package org.tpwport.android;

import android.app.ActivityManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.hardware.input.InputManager;
import android.os.Bundle;
import android.util.Log;
import android.util.SparseBooleanArray;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.ViewConfiguration;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;

import org.libsdl.app.SDLActivity;
import java.io.File;
import java.util.EnumSet;

public final class ThemeParkActivity extends SDLActivity implements InputManager.InputDeviceListener {
    private static final int[] ARROWS = {
        KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_DPAD_RIGHT
    };
    static final int LEFT = MotionEvent.BUTTON_PRIMARY;
    static final int RIGHT = MotionEvent.BUTTON_SECONDARY;
    private static final int[] MOUSE_BUTTONS = {LEFT, RIGHT};
    private static final int[] MODIFIER_KEYS = {KeyEvent.KEYCODE_CTRL_LEFT, KeyEvent.KEYCODE_SHIFT_LEFT};
    private static final float TRIGGER_PRESS = 0.55f;
    private static final float TRIGGER_RELEASE = 0.40f;

    private TouchControls controls;
    private ControlSettings settings;
    private final SparseBooleanArray padKeys = new SparseBooleanArray();
    private int sentArrows;
    private int controller = -1;
    private int pressedArrows, hatArrows, mouseButtons;
    private int padButtons, gamePointerButton;
    private int padModifiers, overlayModifiers;
    private float touchCameraX, touchCameraY, stickCameraX, stickCameraY;
    private int gamePointer = -1, secondPointer = -1;
    private boolean moved, twoFingerTap, sliderDragRequested;
    private float downX, downY, lastX, lastY, secondX, secondY;
    private long touchStart;
    private int touchSlop;
    private final Runnable beginTouchDrag = () -> {
        if (gamePointer < 0 || moved || twoFingerTap) return;
        setTouchButton(LEFT);
    };
    private boolean menuDown, enterDown, menuWhenFocused;
    private boolean showingControls;
    private final Runnable clearControlFocus = () -> showingControls = false;
    private float pointerX = 0.5f, pointerY = 0.5f;
    private float padX, padY;
    private float stickX, stickY;
    private final int[] surfaceOffset = new int[2];
    private long pointerTick;

    private static native void nativeCamera(float x, float y);
    private static native void nativeKeyboard();
    private static native void nativeRotate(int direction);
    private static native void nativeControlDialog(boolean opening);
    private static native void nativeResumePark();
    private static native void nativeSliderDrag(boolean start, boolean touchpad);

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "tpw_launcher" };
    }

    @Override
    protected String[] getArguments() {
        String edition = getIntent().getStringExtra(SetupActivity.EDITION);
        String language = getIntent().getStringExtra(SetupActivity.LANGUAGE);
        int scale = getIntent().getIntExtra(SetupActivity.INTERFACE_SCALE, 100);
        return new String[] { edition == null ? "eur-pc" : edition, language == null ? "" : language,
            getString(R.string.storage_unavailable), getString(R.string.game_start_failed), Integer.toString(scale) };
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (mLayout == null) return;
        describeGameTask();
        settings = new ControlSettings(this);
        controls = new TouchControls(this, settings, getIntent().getIntExtra(SetupActivity.INTERFACE_SCALE, 100) > 100);
        mLayout.setMotionEventSplittingEnabled(true);
        mLayout.addView(controls, new ViewGroup.LayoutParams(-1, -1));
        touchSlop = ViewConfiguration.get(this).getScaledTouchSlop();
        mLayout.post(this::immersive);
        getSystemService(InputManager.class).registerInputDeviceListener(this, null);
    }

    @SuppressWarnings("deprecation") // The Bitmap constructor also works on API 33–36.
    private void describeGameTask() {
        String edition = getIntent().getStringExtra(SetupActivity.EDITION);
        String title = getString(edition != null && edition.startsWith("us")
            ? R.string.game_name_us : R.string.game_name_europe);
        // The shared importer keeps the icon from the player's discs next to launcher.ini.
        Bitmap importedIcon = BitmapFactory.decodeFile(new File(getFilesDir(), "icon.png").getPath());
        ActivityManager.TaskDescription description = importedIcon == null
            ? new ActivityManager.TaskDescription.Builder().setLabel(title).setIcon(R.mipmap.tpw_game_icon).build()
            : new ActivityManager.TaskDescription(title, importedIcon);
        setTitle(title);
        setTaskDescription(description);
        Log.i("TPWLauncher", "game app: " + title + " (" + (importedIcon == null ? "stand-in" : "imported icon") + ")");
    }

    @Override
    public void onWindowFocusChanged(boolean focused) {
        if (controls == null) {
            super.onWindowFocusChanged(focused);
            return;
        }
        mLayout.removeCallbacks(clearControlFocus);
        if (!focused) cancelTouchPointer();
        boolean ownDialog = !focused && showingControls;
        if (ownDialog) nativeControlDialog(true);
        try {
            super.onWindowFocusChanged(focused);
        } finally {
            if (ownDialog) nativeControlDialog(false);
        }
        if (focused) {
            // Replacing a settings dialog can briefly return focus to the Activity.
            if (showingControls) mLayout.postDelayed(clearControlFocus, 150);
            immersive();
            if (menuWhenFocused) {
                menuWhenFocused = false;
                menu();
            }
        }
    }

    private void immersive() {
        getWindow().setDecorFitsSystemWindows(false);
        WindowInsetsController insets = getWindow().getInsetsController();
        if (insets == null) return;
        insets.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        insets.hide(WindowInsets.Type.systemBars());
    }

    @Override
    protected void onPause() {
        if (controls != null) {
            mLayout.removeCallbacks(clearControlFocus);
            showingControls = false;
            menuWhenFocused = false;
            releaseInputs();
        }
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        if (controls != null) getSystemService(InputManager.class).unregisterInputDeviceListener(this);
        super.onDestroy();
        if (isFinishing()) android.os.Process.killProcess(android.os.Process.myPid());
    }

    @Override public void onInputDeviceAdded(int id) {}
    @Override public void onInputDeviceChanged(int id) {}

    @Override
    public void onInputDeviceRemoved(int id) {
        if (id == controller) releaseController();
    }

    void releaseInputs() {
        releaseController();
        cancelTouchPointer();
        controls.clearModifiers();
        controls.cancelStick();
    }

    private void releaseController() {
        controller = -1;
        padX = padY = 0;
        updatePointerStick();
        stickCameraX = stickCameraY = 0;
        updateCamera();
        pressedArrows = hatArrows = 0;
        padKeys.clear();
        refreshHeldActions();
    }

    private static InputDevice.MotionRange triggerRange(InputDevice device, int key) {
        if (device == null || (key != KeyEvent.KEYCODE_BUTTON_L2 && key != KeyEvent.KEYCODE_BUTTON_R2)) return null;
        boolean left = key == KeyEvent.KEYCODE_BUTTON_L2;
        InputDevice.MotionRange range = device.getMotionRange(left ? MotionEvent.AXIS_LTRIGGER : MotionEvent.AXIS_RTRIGGER);
        if (range != null && range.getRange() > 0) return range;
        range = device.getMotionRange(left ? MotionEvent.AXIS_BRAKE : MotionEvent.AXIS_GAS);
        return range != null && range.getRange() > 0 ? range : null;
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        InputDevice device = event.getDevice();
        boolean pad = event.isFromSource(InputDevice.SOURCE_GAMEPAD) || event.isFromSource(InputDevice.SOURCE_DPAD) ||
                      (device != null && device.supportsSource(InputDevice.SOURCE_GAMEPAD));
        int action = event.getAction();
        if (controls == null || !pad || (action != KeyEvent.ACTION_DOWN && action != KeyEvent.ACTION_UP)) {
            return super.dispatchKeyEvent(event);
        }
        if (controls.editingLayout()) return true;
        int key = event.getKeyCode();
        if (settings.binding(key) == null) return super.dispatchKeyEvent(event);
        // Use one source per trigger. Some pads also send button events at full travel.
        if (triggerRange(device, key) != null) return true;
        if (controller != event.getDeviceId()) {
            releaseController();
            controller = event.getDeviceId();
        }
        boolean down = action == KeyEvent.ACTION_DOWN;
        for (int i = 0; i < ARROWS.length; ++i) {
            if (key != ARROWS[i]) continue;
            setArrowSources(bitState(pressedArrows, 1 << i, down), hatArrows);
            return true;
        }
        setPadKey(key, down);
        return true;
    }

    private void setPadKey(int key, boolean down) {
        if (padKeys.get(key) == down) return;
        if (down) padKeys.put(key, true);
        else padKeys.delete(key);
        refreshHeldActions();
        if (!down) return;
        switch (settings.binding(key)) {
            case ZOOM_IN: zoom(1); break;
            case ZOOM_OUT: zoom(-1); break;
            case ROTATE_LEFT: rotate(-1); break;
            case ROTATE_RIGHT: rotate(1); break;
            case KEYBOARD: openKeyboard(); break;
            case OVERLAY: controls.toggle(); break;
            case SETTINGS: showControlSettings(); break;
            case BUY: press(KeyEvent.KEYCODE_B); break;
            case MAP: press(KeyEvent.KEYCODE_SPACE); break;
            case PAUSE: press(KeyEvent.KEYCODE_P); break;
            case HOME: press(KeyEvent.KEYCODE_MOVE_HOME); break;
            case BACKSPACE: backspace(); break;
            default: break;
        }
    }

    private void refreshHeldActions() {
        EnumSet<ControlSettings.Action> actions = EnumSet.noneOf(ControlSettings.Action.class);
        for (int i = 0; i < padKeys.size(); ++i) actions.add(settings.binding(padKeys.keyAt(i)));
        int arrows = (actions.contains(ControlSettings.Action.UP) ? 1 : 0) |
                     (actions.contains(ControlSettings.Action.ARROW_LEFT) ? 2 : 0) |
                     (actions.contains(ControlSettings.Action.DOWN) ? 4 : 0) |
                     (actions.contains(ControlSettings.Action.ARROW_RIGHT) ? 8 : 0);
        for (int i = 0; i < ARROWS.length; ++i) {
            int bit = 1 << i;
            setHeldKey(ARROWS[i], (sentArrows & bit) != 0, (arrows & bit) != 0);
        }
        sentArrows = arrows;
        boolean menu = actions.contains(ControlSettings.Action.MENU);
        boolean enter = actions.contains(ControlSettings.Action.ENTER);
        setHeldKey(KeyEvent.KEYCODE_ESCAPE, menuDown, menu);
        setHeldKey(KeyEvent.KEYCODE_ENTER, enterDown, enter);
        menuDown = menu;
        enterDown = enter;
        int modifiers = (actions.contains(ControlSettings.Action.CTRL) ? 1 : 0) |
                        (actions.contains(ControlSettings.Action.SHIFT) ? 2 : 0);
        setModifiers(modifiers, overlayModifiers);
        padButtons = (actions.contains(ControlSettings.Action.LEFT) ? LEFT : 0) |
                     (actions.contains(ControlSettings.Action.RIGHT) ? RIGHT : 0);
        applyMouseButtons();
    }

    private void updateTrigger(MotionEvent event, InputDevice.MotionRange range, int key, int sample) {
        if (range == null || controller != event.getDeviceId()) return;
        int axis = range.getAxis();
        float value = sample < event.getHistorySize() ? event.getHistoricalAxisValue(axis, sample) : event.getAxisValue(axis);
        float travel = (value - range.getMin()) / range.getRange();
        setPadKey(key, travel >= (padKeys.get(key) ? TRIGGER_RELEASE : TRIGGER_PRESS));
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (controls != null && event.isFromSource(InputDevice.SOURCE_JOYSTICK) && event.getAction() == MotionEvent.ACTION_MOVE) {
            if (controls.editingLayout()) return true;
            if (controller != event.getDeviceId()) {
                releaseController();
                controller = event.getDeviceId();
            }
            padX = event.getAxisValue(MotionEvent.AXIS_X);
            padY = event.getAxisValue(MotionEvent.AXIS_Y);
            updatePointerStick();
            InputDevice device = event.getDevice();
            boolean useZ = device != null && device.getMotionRange(MotionEvent.AXIS_Z, event.getSource()) != null &&
                           device.getMotionRange(MotionEvent.AXIS_RZ, event.getSource()) != null;
            stickCameraX = event.getAxisValue(useZ ? MotionEvent.AXIS_Z : MotionEvent.AXIS_RX);
            stickCameraY = event.getAxisValue(useZ ? MotionEvent.AXIS_RZ : MotionEvent.AXIS_RY);
            updateCamera();
            setArrowSources(pressedArrows, directions(event.getAxisValue(MotionEvent.AXIS_HAT_X), event.getAxisValue(MotionEvent.AXIS_HAT_Y)));
            InputDevice.MotionRange leftTrigger = triggerRange(device, KeyEvent.KEYCODE_BUTTON_L2);
            InputDevice.MotionRange rightTrigger = triggerRange(device, KeyEvent.KEYCODE_BUTTON_R2);
            for (int sample = 0; sample <= event.getHistorySize(); ++sample) {
                updateTrigger(event, leftTrigger, KeyEvent.KEYCODE_BUTTON_L2, sample);
                updateTrigger(event, rightTrigger, KeyEvent.KEYCODE_BUTTON_R2, sample);
            }
            return true;
        }
        if (mSurface != null && event.isFromSource(InputDevice.SOURCE_MOUSE) && event.getAction() == MotionEvent.ACTION_HOVER_MOVE) {
            rememberPointer(event, 0);
        }
        return super.dispatchGenericMotionEvent(event);
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        if (controls == null || !event.isFromSource(InputDevice.SOURCE_TOUCHSCREEN)) return super.dispatchTouchEvent(event);
        if (controls.editingLayout()) return super.dispatchTouchEvent(event);
        int action = event.getActionMasked();
        int index = event.getActionIndex();
        boolean down = action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN;
        if (down && !controls.ownsPoint(event.getX(index), event.getY(index))) {
            if (gamePointer < 0 && secondPointer < 0) {
                gamePointer = event.getPointerId(index);
                downX = lastX = event.getRawX(index);
                downY = lastY = event.getRawY(index);
                touchStart = event.getEventTime();
                moved = twoFingerTap = false;
                if (!settings.touchpadMode()) aimAtTouch(event, index);
                mLayout.postDelayed(beginTouchDrag, ViewConfiguration.getLongPressTimeout());
            } else if (gamePointer >= 0 && secondPointer < 0 && !twoFingerTap && !sliderDragRequested && gamePointerButton == 0) {
                secondPointer = event.getPointerId(index);
                secondX = event.getRawX(index);
                secondY = event.getRawY(index);
                twoFingerTap = true;
            } else moved = true;
        }
        if (action == MotionEvent.ACTION_MOVE) moveTouchPointer(event);
        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) finishTouch(event, index);
        if (action == MotionEvent.ACTION_CANCEL) cancelTouchPointer();
        return super.dispatchTouchEvent(event);
    }

    private void moveTouchPointer(MotionEvent event) {
        int primary = event.findPointerIndex(gamePointer);
        int other = event.findPointerIndex(secondPointer);
        if (other >= 0 && outsideSlop(event, other, secondX, secondY)) moved = true;
        if (primary < 0) return;
        boolean touchpad = settings.touchpadMode();
        boolean motion = event.getActionMasked() == MotionEvent.ACTION_MOVE;
        if (!moved && outsideSlop(event, primary, downX, downY)) {
            moved = true;
            if (motion && !twoFingerTap && (mouseButtons & LEFT) == 0) {
                // Look for a slider before the first move takes the cursor away in either mode.
                sliderDragRequested = true;
                nativeSliderDrag(true, touchpad);
            }
        }
        if (twoFingerTap) return;
        if (!touchpad) {
            if (moved || gamePointerButton != 0 || !motion) aimAtTouch(event, primary);
        } else if ((moved || gamePointerButton != 0) && mSurface != null && mSurface.getWidth() > 1 && mSurface.getHeight() > 1) {
            float x = event.getRawX(primary), y = event.getRawY(primary);
            float scale = settings.cursorScale() * 1.2f;
            pointerX = clamp(pointerX + (x - lastX) * scale / mSurface.getWidth());
            pointerY = clamp(pointerY + (y - lastY) * scale / mSurface.getHeight());
            lastX = x;
            lastY = y;
            mouse(MotionEvent.ACTION_MOVE);
        }
    }

    private boolean outsideSlop(MotionEvent event, int index, float x, float y) {
        return Math.hypot(event.getRawX(index) - x, event.getRawY(index) - y) > touchSlop;
    }

    private void aimAtTouch(MotionEvent event, int index) {
        rememberPointer(event, index);
        mouse(MotionEvent.ACTION_MOVE);
    }

    private void finishTouch(MotionEvent event, int index) {
        int id = event.getPointerId(index);
        if (id != gamePointer && id != secondPointer) return;
        moveTouchPointer(event);
        if (id == gamePointer) gamePointer = -1;
        else secondPointer = -1;
        if (gamePointer >= 0 || secondPointer >= 0) return;
        mLayout.removeCallbacks(beginTouchDrag);
        finishSliderDrag();
        if (gamePointerButton != 0) {
            setTouchButton(0);
            return;
        }
        if (moved || event.getEventTime() - touchStart > ViewConfiguration.getLongPressTimeout()) return;
        clickPointer(twoFingerTap ? RIGHT : LEFT);
    }

    private void cancelTouchPointer() {
        mLayout.removeCallbacks(beginTouchDrag);
        gamePointer = secondPointer = -1;
        finishSliderDrag();
        setTouchButton(0);
    }

    private void finishSliderDrag() {
        if (!sliderDragRequested) return;
        sliderDragRequested = false;
        nativeSliderDrag(false, false);
    }

    private void setTouchButton(int button) {
        gamePointerButton = button;
        applyMouseButtons();
    }

    private void clickPointer(int button) {
        int held = gamePointerButton;
        setTouchButton(held | button);
        setTouchButton(held);
    }

    void rightClick() { clickPointer(RIGHT); }

    void backspace() { press(KeyEvent.KEYCODE_DEL); }

    private static int directions(float x, float y) {
        return (y < -0.30f ? 1 : 0) | (x < -0.30f ? 2 : 0) | (y > 0.30f ? 4 : 0) | (x > 0.30f ? 8 : 0);
    }

    void setOnScreenStick(float x, float y) {
        touchCameraX = x;
        touchCameraY = y;
        updateCamera();
    }

    private void updateCamera() {
        boolean touch = touchCameraX != 0 || touchCameraY != 0;
        float x = touch ? touchCameraX : stickCameraX;
        float y = touch ? touchCameraY : stickCameraY;
        float length = (float) Math.hypot(x, y);
        float strength = length <= 0.18f ? 0 : clamp((length - 0.18f) / 0.82f) * settings.cameraScale() / length;
        nativeCamera(x * strength, y * strength);
    }

    private void setArrowSources(int keys, int hat) {
        int previous = pressedArrows | hatArrows;
        int next = keys | hat;
        pressedArrows = keys;
        hatArrows = hat;
        for (int i = 0; i < ARROWS.length; ++i) {
            int bit = 1 << i;
            if (((previous ^ next) & bit) != 0) setPadKey(ARROWS[i], (next & bit) != 0);
        }
    }

    private static void setHeldKey(int code, boolean before, boolean after) {
        if (before == after) return;
        if (after) onNativeKeyDown(code);
        else onNativeKeyUp(code);
    }

    void menu() {
        if (menuDown) return;
        if (hasWindowFocus()) press(KeyEvent.KEYCODE_ESCAPE);
        else menuWhenFocused = true;
    }

    void press(int code) {
        onNativeKeyDown(code);
        onNativeKeyUp(code);
    }

    void openKeyboard() { nativeKeyboard(); }

    void rotate(int direction) { nativeRotate(direction); }

    void zoom(int amount) {
        onNativeMouse(0, MotionEvent.ACTION_SCROLL, 0, amount, false);
    }

    private static int bitState(int mask, int bit, boolean on) { return on ? mask | bit : mask & ~bit; }

    private void applyMouseButtons() {
        int next = padButtons | gamePointerButton;
        for (int button : MOUSE_BUTTONS) {
            if (((mouseButtons ^ next) & button) == 0) continue;
            boolean down = (next & button) != 0;
            mouseButtons = bitState(mouseButtons, button, down);
            mouse(down ? MotionEvent.ACTION_DOWN : MotionEvent.ACTION_UP);
        }
    }

    void setOverlayModifiers(int modifiers) { setModifiers(padModifiers, modifiers); }

    private void setModifiers(int pad, int overlay) {
        int previous = padModifiers | overlayModifiers;
        int next = pad | overlay;
        for (int i = 0; i < MODIFIER_KEYS.length; ++i) {
            int bit = 1 << i;
            setHeldKey(MODIFIER_KEYS[i], (previous & bit) != 0, (next & bit) != 0);
        }
        padModifiers = pad;
        overlayModifiers = overlay;
    }

    private void mouse(int action) {
        if (mSurface == null) return;
        onNativeMouse(mouseButtons, action, pointerX * (mSurface.getWidth() - 1), pointerY * (mSurface.getHeight() - 1), false);
    }

    private void rememberPointer(MotionEvent event, int index) {
        if (mSurface == null || mSurface.getWidth() <= 1 || mSurface.getHeight() <= 1) return;
        mSurface.getLocationOnScreen(surfaceOffset);
        pointerX = clamp((event.getRawX(index) - surfaceOffset[0]) / (mSurface.getWidth() - 1));
        pointerY = clamp((event.getRawY(index) - surfaceOffset[1]) / (mSurface.getHeight() - 1));
    }

    private static float clamp(float value) { return Math.max(0, Math.min(1, value)); }

    private void updatePointerStick() {
        setStick(padX, padY);
    }

    private void setStick(float x, float y) {
        float magnitude = (float) Math.hypot(x, y);
        float speed = clamp((magnitude - 0.16f) / 0.84f);
        if (speed == 0) {
            stickX = stickY = 0;
            pointerTick = 0;
            if (mSurface != null) mSurface.removeCallbacks(movePointer);
            return;
        }
        stickX = x / magnitude * speed * speed;
        stickY = y / magnitude * speed * speed;
        if (pointerTick == 0 && mSurface != null) {
            pointerTick = System.nanoTime();
            mSurface.postOnAnimation(movePointer);
        }
    }

    private final Runnable movePointer = new Runnable() {
        @Override
        public void run() {
            if (pointerTick == 0 || mSurface == null || mSurface.getWidth() <= 1 || mSurface.getHeight() <= 1) {
                pointerTick = 0;
                return;
            }
            long now = System.nanoTime();
            float delta = Math.min((now - pointerTick) / 1_000_000_000f, 0.05f);
            pointerTick = now;
            float aspect = (float) mSurface.getWidth() / mSurface.getHeight();
            float speed = 1.4f * settings.cursorScale();
            float nextX = clamp(pointerX + delta * stickX * speed / aspect);
            float nextY = clamp(pointerY + delta * stickY * speed);
            if (gamePointer < 0 && (nextX != pointerX || nextY != pointerY)) {
                pointerX = nextX;
                pointerY = nextY;
                mouse(MotionEvent.ACTION_MOVE);
            }
            mSurface.postOnAnimation(this);
        }
    };

    void showControlSettings() {
        releaseInputs();
        showingControls = true;
        mLayout.removeCallbacks(clearControlFocus);
        settings.show(this);
    }

    void editTouchLayout() { controls.beginLayoutEdit(); }

    void toggleBuildControls() { controls.toggleBuilding(); }

    void toggleCameraControls() { controls.toggle(); }

    void parkAvailable(boolean available) {
        runOnUiThread(() -> {
            if (controls != null && !isFinishing()) controls.setParkLoaded(available);
        });
    }

    void cameraAvailable(boolean active) {
        runOnUiThread(() -> {
            if (controls != null && !isFinishing()) controls.setParkActive(active);
        });
    }

    void parkResumeAvailable(boolean available) {
        runOnUiThread(() -> {
            if (controls != null && !isFinishing()) controls.setCanResumePark(available);
        });
    }

    void resumePark() { nativeResumePark(); }

    void controlSettingsReset() { releaseInputs(); controls.updateVisibility(); }

    @Override
    public void onBackPressed() {
        if (controls != null && controls.editingLayout()) controls.finishLayoutEdit();
        else menu();
    }
}

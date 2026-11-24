package org.tpwport.android;

import android.graphics.Canvas;
import android.graphics.Insets;
import android.graphics.Paint;
import android.graphics.PointF;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.text.TextUtils;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.FrameLayout;
import java.util.ArrayList;
import java.util.List;

final class TouchControls extends FrameLayout {
    private static final int BUTTON_SIZE = 48;
    private final ThemeParkActivity game;
    private final ControlSettings settings;
    private final boolean largerInterface;
    private final List<View> toolbar = new ArrayList<>(), editingShortcuts = new ArrayList<>(), building = new ArrayList<>();
    private final Button ctrl, shift;
    private final Button gear, resetLayout, resumePark;
    private final Button fly, rise, descend, resetFlight;
    private final CameraStick stick, flightMove;
    private boolean editing, editingFlight, parkLoaded, parkActive, canResumePark;
    private boolean flightAvailable, flying, flightControlsVisible;
    private int modifiers;
    private View dragging;
    private int dragFinger = -1;
    private float dragX, dragY;

    TouchControls(ThemeParkActivity game, ControlSettings settings, boolean largerInterface) {
        super(game);
        this.game = game;
        this.settings = settings;
        this.largerInterface = largerInterface;
        setClipChildren(false);
        setMotionEventSplittingEnabled(true);

        button(building, "buy", game.getString(R.string.buy), game.getString(R.string.buy_rides), () -> game.press(KeyEvent.KEYCODE_B));
        button(building, "rotate_left", "↺", game.getString(R.string.rotate_left), () -> game.rotate(-1));
        button(building, "rotate_right", "↻", game.getString(R.string.rotate_right), () -> game.rotate(1));
        ctrl = button(building, "ctrl", "Ctrl", game.getString(R.string.hold_ctrl), () -> toggleModifier(1));
        shift = button(building, "shift", "Shift", game.getString(R.string.hold_shift), () -> toggleModifier(2));
        button(building, "map", game.getString(R.string.map), game.getString(R.string.show_map), () -> game.press(KeyEvent.KEYCODE_SPACE));

        button(toolbar, "zoom_out", "−", game.getString(R.string.zoom_out), () -> game.zoom(-1));
        button(toolbar, "zoom_in", "+", game.getString(R.string.zoom_in), () -> game.zoom(1));
        descend = heightButton("flight_descend", "↓", R.string.free_camera_descend, 1);
        rise = heightButton("flight_rise", "↑", R.string.free_camera_rise, 2);
        fly = button(toolbar, "free_camera", "↗", game.getString(R.string.free_camera), game::toggleFreeCamera);
        button(editingShortcuts, "right_click", game.getString(R.string.right_mouse_short), game.getString(R.string.right_mouse), game::rightClick);
        button(editingShortcuts, "backspace", "⌫", game.getString(R.string.backspace), game::backspace);
        resetFlight = button(editingShortcuts, "flight_reset", game.getString(R.string.free_camera_reset_short),
            game.getString(R.string.free_camera_reset), game::resetFreeCamera);

        stick = new CameraStick(false);
        stick.setTag("stick");
        addView(stick, new LayoutParams(dp(120), dp(120)));
        flightMove = new CameraStick(true);
        flightMove.setTag("flight_move");
        addView(flightMove, new LayoutParams(dp(120), dp(120)));
        gear = button(null, null, "⚙", game.getString(R.string.control_settings), () -> {
            if (editing) finishLayoutEdit();
            else game.showControlSettings();
        });
        resetLayout = button(null, null, game.getString(R.string.reset_layout), game.getString(R.string.restore_layout), () -> {
            settings.resetPositions();
            requestLayout();
        });
        String resumeLabel = game.getString(R.string.resume_park);
        resumePark = button(null, null, resumeLabel, resumeLabel, game::resumePark);
        updateVisibility();
    }

    private Button heightButton(String tag, String arrow, int label, int bit) {
        Button height = button(toolbar, tag, arrow, game.getString(label), () -> {});
        height.setOnTouchListener((view, event) -> {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN: game.holdFlightHeight(bit, true); break;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL: game.holdFlightHeight(bit, false); break;
                default: break;
            }
            return false;
        });
        return height;
    }

    private int dp(float size) { return Math.round(size * getResources().getDisplayMetrics().density); }

    private GradientDrawable pill(int fill) {
        GradientDrawable shape = new GradientDrawable();
        shape.setColor(fill);
        shape.setCornerRadius(dp(16));
        shape.setStroke(dp(1), 0x90BBDEF1);
        return shape;
    }

    private Button button(List<View> row, String id, String text, String description, Runnable action) {
        Button button = new Button(game);
        button.setText(text);
        button.setContentDescription(description);
        button.setAllCaps(false);
        button.setTextColor(0xFFF4FAFF);
        button.setTextSize(14);
        button.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        button.setSingleLine(true);
        button.setEllipsize(TextUtils.TruncateAt.END);
        button.setPadding(dp(7), 0, dp(7), 0);
        button.setMinimumWidth(dp(BUTTON_SIZE));
        button.setMinWidth(dp(BUTTON_SIZE));
        button.setFocusable(false);
        button.setSoundEffectsEnabled(false);
        StateListDrawable background = new StateListDrawable();
        background.addState(new int[] {android.R.attr.state_pressed}, pill(0xEC194353));
        background.addState(new int[] {android.R.attr.state_selected}, pill(0xE620687E));
        background.addState(new int[0], pill(0xBB142433));
        button.setBackground(background);
        button.setOnClickListener(view -> action.run());
        button.setTag(id);
        if (row != null) row.add(button);
        addView(button, new LayoutParams(LayoutParams.WRAP_CONTENT, dp(BUTTON_SIZE)));
        return button;
    }

    @Override
    protected void onLayout(boolean changed, int leftEdge, int top, int rightEdge, int bottom) {
        int width = rightEdge - leftEdge, height = bottom - top;
        int stickWidth = stick.getMeasuredWidth();
        int stickX = width - stickWidth - dp(20);
        int stickY = height / 5;
        int zoomY = stickY + stick.getMeasuredHeight() + dp(4);
        int panelCenter = stickX + stickWidth / 2;
        layoutRow(toolbar, width, panelCenter, zoomY, 0);
        layoutRow(editingShortcuts, width, panelCenter, zoomY + dp(BUTTON_SIZE + 6), 0);
        int buildLeft = 0;
        if (largerInterface) {
            // A centred 4:3 viewport puts the enlarged park toolbar farthest to the right.
            int gameHeight = Math.min(height, Math.round(width * 0.75f));
            int gameLeft = (width - Math.round(gameHeight * (4f / 3f))) / 2;
            buildLeft = Math.min(width / 2, gameLeft + Math.round(gameHeight * 0.40f) + dp(8));
        }
        layoutRow(building, width, width / 2, height - dp(76), buildLeft);
        if (stick.getVisibility() == VISIBLE) place(stick, stickX, stickY, stickWidth);
        int gearWidth = gear.getMeasuredWidth();
        WindowInsets insets = getRootWindowInsets();
        Insets cutout = insets == null ? Insets.NONE
            : insets.getInsetsIgnoringVisibility(WindowInsets.Type.displayCutout());
        if (flightMove.getVisibility() == VISIBLE) place(flightMove, dp(20) + cutout.left, stickY, flightMove.getMeasuredWidth());
        // The player list is on the right in the lobby. The park's cash is on the left.
        int gearX = editing || parkLoaded ? width - gearWidth - dp(12) - cutout.right : dp(12) + cutout.left;
        place(gear, gearX, dp(10) + cutout.top, gearWidth);
        if (editing) place(resetLayout, dp(12), dp(10), resetLayout.getMeasuredWidth());
        if (resumePark.getVisibility() == VISIBLE) {
            int buttonWidth = resumePark.getMeasuredWidth();
            place(resumePark, (width - buttonWidth) / 2, height * 2 / 3, buttonWidth);
        }
    }

    private void layoutRow(List<View> row, int width, int center, int y, int minLeft) {
        int total = 0, count = 0;
        for (View view : row) if (view.getVisibility() == VISIBLE) { total += view.getMeasuredWidth(); ++count; }
        int spacing = dp(6);
        int available = Math.max(1, width - minLeft - (count + 1) * spacing);
        float scale = total > available ? (float) available / total : 1;
        int rowWidth = (int) (total * scale) + Math.max(0, count - 1) * spacing;
        int x = minLeft + clamp(center - rowWidth / 2 - minLeft, Math.max(0, width - rowWidth - minLeft));
        for (View view : row) {
            if (view.getVisibility() != VISIBLE) continue;
            int cellWidth = Math.max(1, (int) (view.getMeasuredWidth() * scale));
            place(view, x, y, cellWidth);
            x += cellWidth + spacing;
        }
    }

    private void place(View view, int x, int y, int width) {
        int height = view.getMeasuredHeight();
        int spanX = Math.max(0, getWidth() - width), spanY = Math.max(0, getHeight() - height);
        PointF saved = view.getTag() instanceof String ? settings.position((String) view.getTag()) : null;
        int left = clamp(saved != null ? Math.round(saved.x * spanX) : x, spanX);
        int top = clamp(saved != null ? Math.round(saved.y * spanY) : y, spanY);
        view.layout(left, top, left + width, top + height);
    }

    private static int clamp(int n, int end) { return Math.max(0, Math.min(n, end)); }

    void updateVisibility() {
        boolean flightLayout = editing ? editingFlight : flying;
        boolean camera = !flightLayout && (editing || settings.cameraVisible() && parkActive);
        boolean showFlight = flightLayout && (editing || flightControlsVisible && parkActive);
        for (View view : toolbar) {
            boolean show = view == fly ? editing || flying || (camera && flightAvailable)
                : view == rise || view == descend ? showFlight : camera;
            view.setVisibility(show ? VISIBLE : GONE);
        }
        for (View view : editingShortcuts) view.setVisibility((view == resetFlight ? showFlight : camera) ? VISIBLE : GONE);
        for (View view : building) view.setVisibility(camera && (editing || settings.buildingVisible()) ? VISIBLE : GONE);
        stick.setVisibility(camera || showFlight ? VISIBLE : GONE);
        stick.setContentDescription(game.getString(flightLayout ? R.string.free_camera_look : R.string.camera_stick));
        flightMove.setVisibility(showFlight ? VISIBLE : GONE);
        resetLayout.setVisibility(editing ? VISIBLE : GONE);
        resumePark.setVisibility(canResumePark && !editing ? VISIBLE : GONE);
        requestLayout();
    }

    void setFlightAvailable(boolean available) {
        flightAvailable = available;
        updateVisibility();
    }

    void setFlying(boolean enabled) {
        flying = enabled;
        flightControlsVisible = enabled;
        fly.setSelected(enabled);
        fly.setText(enabled ? "×" : "↗");
        fly.setContentDescription(game.getString(enabled ? R.string.free_camera_exit : R.string.free_camera));
        cancelStick();
        updateVisibility();
    }

    void setParkActive(boolean active) {
        if (parkActive == active) return;
        parkActive = active;
        if (!active) {
            cancelStick();
            clearModifiers();
        }
        updateVisibility();
    }

    void setParkLoaded(boolean loaded) {
        if (parkLoaded == loaded) return;
        parkLoaded = loaded;
        requestLayout();
    }

    void setCanResumePark(boolean available) {
        canResumePark = available;
        updateVisibility();
    }

    void toggle() {
        if (flying) {
            flightControlsVisible = !flightControlsVisible;
            if (!flightControlsVisible) cancelStick();
            updateVisibility();
            return;
        }
        boolean visible = !settings.cameraVisible();
        settings.setCameraVisible(visible);
        updateVisibility();
        if (!visible) {
            cancelStick();
            clearModifiers();
        }
    }

    void toggleBuilding() {
        boolean visible = !settings.buildingVisible();
        settings.setBuildingVisible(visible);
        updateVisibility();
        if (!visible) clearModifiers();
    }

    private void toggleModifier(int bit) { setModifiers(modifiers ^ bit); }

    void clearModifiers() { setModifiers(0); }

    private void setModifiers(int next) {
        modifiers = next;
        ctrl.setSelected((modifiers & 1) != 0);
        shift.setSelected((modifiers & 2) != 0);
        game.setOverlayModifiers(modifiers);
    }

    void cancelStick() {
        stick.reset();
        flightMove.reset();
        game.holdFlightHeight(1, false);
        game.holdFlightHeight(2, false);
    }

    boolean ownsPoint(float x, float y) {
        for (int i = getChildCount() - 1; i >= 0; --i) if (inside(getChildAt(i), x, y)) return true;
        return false;
    }

    boolean editingLayout() { return editing; }

    void beginLayoutEdit(boolean flightLayout) {
        editing = true;
        editingFlight = flightLayout;
        gear.setText(game.getString(R.string.done));
        gear.setContentDescription(game.getString(R.string.finish_move_controls));
        updateVisibility();
    }

    void finishLayoutEdit() {
        editing = false;
        dragging = null;
        dragFinger = -1;
        gear.setText("⚙");
        gear.setContentDescription(game.getString(R.string.control_settings));
        updateVisibility();
    }

    @Override
    public boolean onInterceptTouchEvent(MotionEvent event) {
        if (!editing) return false;
        float x = event.getX(), y = event.getY();
        return event.getActionMasked() == MotionEvent.ACTION_DOWN && !inside(gear, x, y) && !inside(resetLayout, x, y);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (!editing) return super.onTouchEvent(event);
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN) {
            for (int i = getChildCount() - 1; i >= 0; --i) {
                View view = getChildAt(i);
                if (view.getTag() == null || !inside(view, event.getX(), event.getY())) continue;
                dragging = view;
                dragFinger = event.getPointerId(0);
                dragX = event.getX() - view.getLeft();
                dragY = event.getY() - view.getTop();
                break;
            }
        }
        if (dragging == null) return true;
        if (action == MotionEvent.ACTION_CANCEL) {
            dragging = null;
            dragFinger = -1;
            requestLayout();
            return true;
        }
        int index = event.findPointerIndex(dragFinger);
        if (index < 0) return true;
        int width = dragging.getWidth(), height = dragging.getHeight();
        int spanX = Math.max(0, getWidth() - width), spanY = Math.max(0, getHeight() - height);
        int x = clamp(Math.round(event.getX(index) - dragX), spanX);
        int y = clamp(Math.round(event.getY(index) - dragY), spanY);
        dragging.layout(x, y, x + width, y + height);
        if ((action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) && event.getActionIndex() == index) {
            settings.savePosition((String) dragging.getTag(), x / (float) Math.max(1, spanX), y / (float) Math.max(1, spanY));
            dragging = null;
            dragFinger = -1;
            performClick();
        }
        return true;
    }

    @Override public boolean performClick() { return super.performClick(); }

    private boolean inside(View view, float x, float y) {
        return view.getVisibility() == VISIBLE && x >= view.getLeft() && x < view.getRight() &&
               y >= view.getTop() && y < view.getBottom();
    }

    private final class CameraStick extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final boolean movement;
        private int finger = -1;
        private float x, y, lastX, lastY;

        CameraStick(boolean movement) {
            super(game);
            this.movement = movement;
            setContentDescription(game.getString(movement ? R.string.free_camera_move : R.string.camera_stick));
        }

        @Override
        protected void onDraw(Canvas canvas) {
            float center = getWidth() / 2f;
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(0x82142433);
            canvas.drawCircle(center, center, center - dp(4), paint);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(dp(1.5f));
            paint.setColor(0x90BBDEF1);
            canvas.drawCircle(center, center, center - dp(4), paint);
            canvas.drawLine(center - dp(37), center, center + dp(37), center, paint);
            canvas.drawLine(center, center - dp(37), center, center + dp(37), paint);
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(finger < 0 ? 0xA5BBDEF1 : 0xE1BBDEF1);
            canvas.drawCircle(center + x * dp(35), center + y * dp(35), dp(18), paint);
            paint.setColor(0xFFF4FAFF);
            paint.setTextSize(dp(10));
            paint.setTextAlign(Paint.Align.CENTER);
            String label = game.getString(movement ? R.string.free_camera_move_short
                : (editing ? editingFlight : flying) ? R.string.free_camera_look_short : R.string.camera);
            canvas.drawText(label, center, center + dp(49), paint);
        }

        @Override
        public boolean onTouchEvent(MotionEvent event) {
            int action = event.getActionMasked();
            if (action == MotionEvent.ACTION_DOWN) {
                finger = event.getPointerId(0);
                lastX = event.getX(0);
                lastY = event.getY(0);
            }
            if (action == MotionEvent.ACTION_CANCEL ||
                ((action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) &&
                 event.getPointerId(event.getActionIndex()) == finger)) {
                reset();
                return true;
            }
            int pointer = event.findPointerIndex(finger);
            if (pointer < 0) return true;
            x = (event.getX(pointer) - getWidth() / 2f) / dp(35);
            y = (event.getY(pointer) - getHeight() / 2f) / dp(35);
            float length = Math.max(1, (float) Math.hypot(x, y));
            x /= length;
            y /= length;
            if (movement) game.moveFreeCamera(x, y);
            else if (flying) {
                float nextX = event.getX(pointer), nextY = event.getY(pointer);
                float density = getResources().getDisplayMetrics().density;
                game.lookFreeCamera((nextX - lastX) / density, (nextY - lastY) / density);
                lastX = nextX;
                lastY = nextY;
            } else game.setOnScreenStick(x, y);
            invalidate();
            return true;
        }

        void reset() {
            finger = -1;
            x = y = 0;
            if (movement) game.moveFreeCamera(0, 0);
            else game.setOnScreenStick(0, 0);
            invalidate();
        }
    }
}

package org.tpwport.android;

import android.app.AlertDialog;
import android.app.Activity;
import android.content.Context;
import android.graphics.PointF;
import android.os.Handler;
import android.os.Looper;
import android.util.AtomicFile;
import android.view.KeyEvent;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Properties;

final class ControlSettings {
    enum Action {
        NONE(R.string.binding_none), LEFT(R.string.left_mouse), RIGHT(R.string.right_mouse),
        UP(R.string.arrow_up), DOWN(R.string.arrow_down), ARROW_LEFT(R.string.arrow_left), ARROW_RIGHT(R.string.arrow_right),
        ZOOM_IN(R.string.zoom_in), ZOOM_OUT(R.string.zoom_out), ROTATE_LEFT(R.string.rotate_left), ROTATE_RIGHT(R.string.rotate_right),
        CTRL(R.string.binding_ctrl), SHIFT(R.string.binding_shift), MENU(R.string.game_menu), ENTER(R.string.enter),
        KEYBOARD(R.string.show_keyboard), OVERLAY(R.string.toggle_overlay),
        SETTINGS(R.string.control_settings), BUY(R.string.buy_rides), MAP(R.string.map), PAUSE(R.string.pause), HOME(R.string.entrance_view);

        final int label;
        Action(int label) { this.label = label; }
    }

    private static final int[] KEYS = {
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_BUTTON_Y,
        KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_BUTTON_L2, KeyEvent.KEYCODE_BUTTON_R2,
        KeyEvent.KEYCODE_BUTTON_THUMBL, KeyEvent.KEYCODE_BUTTON_THUMBR, KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_BUTTON_SELECT,
        KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_DPAD_RIGHT
    };
    private static final String[] NAMES = {"A", "B", "X", "Y", "L1", "R1", "L2", "R2", "L3", "R3", "Start", "Select", "↑", "←", "↓", "→"};
    private static final Action[] DEFAULTS = {
        Action.LEFT, Action.RIGHT, Action.KEYBOARD, Action.ENTER,
        Action.ZOOM_OUT, Action.ZOOM_IN, Action.ROTATE_LEFT, Action.ROTATE_RIGHT,
        Action.CTRL, Action.SHIFT, Action.MENU, Action.OVERLAY,
        Action.UP, Action.ARROW_LEFT, Action.DOWN, Action.ARROW_RIGHT
    };
    private static final String CURSOR_SPEED = "cursor_speed";
    private static final String CAMERA_SPEED = "camera_speed";
    private static final String POSITION = "position_";
    private final Properties preferences = new Properties();
    private final AtomicFile file;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable saveLater = this::save;
    private final Action[] bindings = new Action[KEYS.length];
    private int cursorSpeed, cameraSpeed;

    ControlSettings(Context context) {
        file = new AtomicFile(new File(context.getFilesDir(), "controls.properties"));
        try (InputStream input = file.openRead()) { preferences.load(input); }
        catch (IOException ignored) { /* A fresh installation uses the defaults. */ }
        read();
    }

    private void save() {
        FileOutputStream output = null;
        try {
            output = file.startWrite();
            preferences.store(output, null);
            file.finishWrite(output);
        } catch (IOException error) {
            file.failWrite(output);
            android.util.Log.e("TPWControls", "Could not save controls", error);
        }
    }

    private void put(String key, String value) { preferences.setProperty(key, value); save(); }

    private int speed(String key) {
        try { return Math.max(25, Math.min(200, Integer.parseInt(preferences.getProperty(key, "100")))); }
        catch (NumberFormatException ignored) { return 100; }
    }

    private void read() {
        cursorSpeed = speed(CURSOR_SPEED);
        cameraSpeed = speed(CAMERA_SPEED);
        for (int i = 0; i < KEYS.length; ++i) {
            String name = preferences.getProperty("key_" + KEYS[i], DEFAULTS[i].name());
            try { bindings[i] = Action.valueOf(name); }
            catch (IllegalArgumentException ignored) { bindings[i] = DEFAULTS[i]; }
        }
    }

    float cursorScale() { return cursorSpeed / 100f; }
    float cameraScale() { return cameraSpeed / 100f; }

    Action binding(int key) {
        if (key == KeyEvent.KEYCODE_BACK) return Action.MENU;
        for (int i = 0; i < KEYS.length; ++i) if (KEYS[i] == key) return bindings[i];
        return null;
    }

    PointF position(String name) {
        String key = POSITION + name;
        if (!preferences.containsKey(key + "_x")) return null;
        try { return new PointF(Float.parseFloat(preferences.getProperty(key + "_x", "0")),
                               Float.parseFloat(preferences.getProperty(key + "_y", "0"))); }
        catch (NumberFormatException ignored) { return null; }
    }

    void savePosition(String name, float x, float y) {
        String key = POSITION + name;
        preferences.setProperty(key + "_x", Float.toString(x));
        put(key + "_y", Float.toString(y));
    }

    void resetPositions() {
        for (String key : preferences.stringPropertyNames()) if (key.startsWith(POSITION)) preferences.remove(key);
        save();
    }

    boolean touchpadMode() { return Boolean.parseBoolean(preferences.getProperty("touchpad_mode", "false")); }
    boolean cameraVisible() { return Boolean.parseBoolean(preferences.getProperty("camera_visible", "true")); }
    boolean buildingVisible() { return Boolean.parseBoolean(preferences.getProperty("building_visible", "false")); }

    void setCameraVisible(boolean visible) { put("camera_visible", Boolean.toString(visible)); }

    void setBuildingVisible(boolean visible) {
        if (visible) preferences.setProperty("camera_visible", "true");
        put("building_visible", Boolean.toString(visible));
    }

    void show(Activity owner) {
        ThemeParkActivity game = owner instanceof ThemeParkActivity ? (ThemeParkActivity) owner : null;
        LinearLayout fields = new LinearLayout(owner);
        fields.setOrientation(LinearLayout.VERTICAL);
        int space = Math.round(22 * owner.getResources().getDisplayMetrics().density);
        fields.setPadding(space, space / 2, space, space / 2);
        slider(owner, fields, owner.getString(R.string.cursor_sensitivity), cursorSpeed, CURSOR_SPEED);
        slider(owner, fields, owner.getString(R.string.camera_sensitivity), cameraSpeed, CAMERA_SPEED);
        AlertDialog dialog = new AlertDialog.Builder(owner).setTitle(R.string.controls)
            .setView(scroll(owner, fields)).setPositiveButton(R.string.done, null).create();
        button(owner, fields, touchpadMode() ? R.string.mouse_mode_touchpad : R.string.mouse_mode_touch, () -> {
            String[] modes = {owner.getString(R.string.touch_mode_hint), owner.getString(R.string.touchpad_mode_hint)};
            new AlertDialog.Builder(owner).setTitle(R.string.mouse_mode).setSingleChoiceItems(modes, touchpadMode() ? 1 : 0, (picker, index) -> {
                put("touchpad_mode", Boolean.toString(index == 1));
                if (game != null) game.releaseInputs();
                picker.dismiss();
                dialog.dismiss();
                show(owner);
            }).setNegativeButton(R.string.cancel, null).show();
        });
        button(owner, fields, R.string.button_mapping, () -> showBindings(owner));
        if (game != null) {
            button(owner, fields, R.string.move_controls, () -> {
                dialog.dismiss();
                game.editTouchLayout();
            });
            button(owner, fields, R.string.toggle_build, () -> {
                dialog.dismiss();
                game.toggleBuildControls();
            });
            button(owner, fields, R.string.toggle_camera, () -> {
                dialog.dismiss();
                game.toggleCameraControls();
            });
            button(owner, fields, R.string.game_menu, () -> {
                dialog.dismiss();
                game.getWindow().getDecorView().post(game::menu);
            });
        }
        button(owner, fields, R.string.restore_controls, () -> new AlertDialog.Builder(owner)
            .setMessage(R.string.restore_controls_confirm)
            .setNegativeButton(R.string.cancel, null).setPositiveButton(R.string.restore, (confirm, which) -> {
                preferences.clear();
                save();
                read();
                if (game != null) game.controlSettingsReset();
                dialog.dismiss();
            }).show());
        dialog.show();
    }

    private static void button(Context context, LinearLayout parent, int label, Runnable action) {
        android.widget.Button button = new android.widget.Button(context);
        button.setText(label);
        button.setAllCaps(false);
        button.setOnClickListener(view -> action.run());
        parent.addView(button);
    }

    private void slider(Context context, LinearLayout parent, String label, int current, String key) {
        TextView value = new TextView(context);
        value.setText(label + " · " + current + "%");
        parent.addView(value);
        SeekBar slider = new SeekBar(context);
        slider.setContentDescription(label);
        slider.setMax(175);
        slider.setProgress(current - 25);
        parent.addView(slider);
        slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onStartTrackingTouch(SeekBar bar) {}
            @Override public void onStopTrackingTouch(SeekBar bar) {
                handler.removeCallbacks(saveLater);
                save();
            }
            @Override public void onProgressChanged(SeekBar bar, int progress, boolean user) {
                int speed = progress + 25;
                value.setText(label + " · " + speed + "%");
                if (!user) return;
                if (CURSOR_SPEED.equals(key)) cursorSpeed = speed;
                else cameraSpeed = speed;
                preferences.setProperty(key, Integer.toString(speed));
                handler.removeCallbacks(saveLater);
                handler.postDelayed(saveLater, 200);
            }
        });
    }

    private void showBindings(Activity owner) {
        String[] rows = new String[KEYS.length];
        for (int i = 0; i < rows.length; ++i) rows[i] = NAMES[i] + "  ·  " + owner.getString(bindings[i].label);
        new AlertDialog.Builder(owner).setTitle(R.string.button_mapping).setItems(rows, (list, index) -> {
            Action[] actions = Action.values();
            String[] names = new String[actions.length];
            for (int i = 0; i < names.length; ++i) names[i] = owner.getString(actions[i].label);
            new AlertDialog.Builder(owner).setTitle(owner.getString(R.string.button_name, NAMES[index]))
                .setSingleChoiceItems(names, bindings[index].ordinal(), (picker, choice) -> {
                    if (owner instanceof ThemeParkActivity) ((ThemeParkActivity) owner).releaseInputs();
                    bindings[index] = actions[choice];
                    put("key_" + KEYS[index], actions[choice].name());
                    picker.dismiss();
                    showBindings(owner);
                }).setNegativeButton(R.string.cancel, (picker, choice) -> showBindings(owner)).show();
        }).setPositiveButton(R.string.done, null).show();
    }

    private static ScrollView scroll(Context context, LinearLayout content) {
        ScrollView scroll = new ScrollView(context);
        scroll.addView(content);
        return scroll;
    }
}

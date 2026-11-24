package org.tpwport.android;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.content.res.Configuration;
import android.database.Cursor;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.LayerDrawable;
import android.graphics.drawable.StateListDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.text.InputType;
import android.util.Log;
import android.view.Gravity;
import android.view.Display;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.stream.Stream;

public final class SetupActivity extends Activity {
    static final String EDITION = "edition";
    static final String LANGUAGE = "language";
    static final String INTERFACE_SCALE = "interface_scale";
    static final String IMMERSIVE = "immersive_full_screen";
    static final String FILL_CUTOUT = "fill_display_cutout";
    static final String RETURN_TO_LAUNCHER = "return_to_launcher";
    private static final String AUTO_UPDATES = "check_for_updates";
    private static final int[] INTERFACE_SCALES = {70, 85, 100, 115, 125};
    private static final int PICK_FILES = 1;
    private static final int PICK_FOLDER = 2;
    private static final int PICK_PACK = 3;
    private static final int PICK_SAVES_US = 4, PICK_SAVES_EUR = 5;
    private static final int EXPORT_SAVES_US = 6, EXPORT_SAVES_EUR = 7;
    private static final int EXPORT_LOG = 8;
    private static final int REFRESH_PS1_FILES = 9, REFRESH_PS1_FOLDER = 10;
    private static final int REFRESH_TPI_FILES = 11, REFRESH_TPI_FOLDER = 12;
    private static final int READ_DATA = 0, VERIFY_DATA = 1, REMOVE_DATA = 2;
    private static final int NAVY = 0xFF0C2032;
    private static final int PAPER = 0xFFF3F9E9;
    private static final int MUTED = 0xFFB7D4CD;
    private static final int GOLD = 0xFFFFD570;
    private static final int INK = 0xFF20291C;

    private enum ButtonStyle {
        NAVIGATION(0xFF2A4A66, 0xFF19344E, 0xFF587D9A, PAPER),
        CHOICE(0xFF214B3E, 0xFF142F2E, 0xFF699778, PAPER),
        IMPORT(0xFF365B61, 0xFF213F49, 0xFF698E91, PAPER),
        PLAY(0xFFFFECA2, 0xFFF4BA43, 0xFFFFD880, INK);

        final int top, bottom, edge, text;

        ButtonStyle(int top, int bottom, int edge, int text) {
            this.top = top;
            this.bottom = bottom;
            this.edge = edge;
            this.text = text;
        }
    }

    private final ExecutorService work = Executors.newSingleThreadExecutor();
    private Button play, files, folder, manageFiles, edition, language, options, mods;
    private Button update;
    private Switch goldRides;
    private TextView gameTitle, status;
    private LinearLayout body, header, quickActions, panels, gamePanel, dataPanel;
    private ProgressBar progress;
    private boolean busy;
    private boolean checkingUpdates;
    private Runnable pendingDocument;
    private String[] editions = {}, europeanLanguages = {}, americanLanguages = {};
    private String[] europeanLabels = {}, americanLabels = {};
    private String[] installed = {};
    private String chosenEdition = "", chosenLanguage = "";
    private boolean goldAvailable, goldUs, goldEur;

    private static native String[] nativeImport(String dataFolder, String[] inputs, int operation, String edition);
    private static native String[] nativeTranslationLanguages();
    private static native boolean nativeNameTranslation(String dataFolder, String id, String language);
    private static native String[] nativeOptions(String dataFolder, boolean mods, String changedKey, boolean enabled);
    private static native String[] nativeChoiceOptions(String dataFolder, boolean camera, String changedKey, int selection,
                                                       int maxFrameRate);
    private static native String[] nativeOnlineServer(String dataFolder, String host);
    private static native String[] nativeEditionDetails(String dataFolder, String edition, String restore);
    private static native String[] nativeGameOptions(String dataFolder, String edition, String profile, int option, int value,
                                                       int screenWidth, int screenHeight);

    @Override
    protected void onCreate(Bundle previous) {
        super.onCreate(previous);
        setTaskDescription(new ActivityManager.TaskDescription.Builder()
            .setLabel(getString(R.string.launcher_name)).setIcon(R.mipmap.tpw_icon).build());
        migrateLanguagePreference();
        createScreen();
        runImport(new ArrayList<>(), false, "");
        checkForUpdates();
    }

    private void createScreen() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackground(gradient(0xFF203D56, NAVY, NAVY, 0, 0));
        FrameLayout page = new FrameLayout(this);
        body = column();
        page.addView(body, new FrameLayout.LayoutParams(-1, -2, Gravity.TOP | Gravity.CENTER_HORIZONTAL));
        scroll.addView(page);
        setContentView(scroll);

        header = new LinearLayout(this);
        header.setBackground(frame(0xFF253F58, 0xFF142A41, 0xFF4B677F, 19));
        header.addView(launcherTitle());
        quickActions = new LinearLayout(this);
        options = launcherButton(R.string.settings, ButtonStyle.NAVIGATION, () -> showOptions(false));
        mods = launcherButton(R.string.mods, ButtonStyle.NAVIGATION, () -> showOptions(true));
        quickActions.addView(options);
        quickActions.addView(mods);
        header.addView(quickActions);
        body.addView(header);
        update = launcherButton(R.string.check_updates, ButtonStyle.NAVIGATION);
        update.setVisibility(View.GONE);
        addRow(body, update, 8);

        gamePanel = panel(0xFF3E7050, 0xFF24483B, 0xFF79A177);
        gameTitle = heading(getString(R.string.select_edition), 30);
        addRow(gamePanel, gameTitle, 0);
        edition = launcherButton(R.string.edition, ButtonStyle.CHOICE, () -> choose(false));
        language = launcherButton(R.string.language, ButtonStyle.CHOICE, () -> choose(true));
        alignStart(edition);
        alignStart(language);
        play = launcherButton(R.string.play, ButtonStyle.PLAY, this::play);
        play.setTextSize(22);
        play.setTypeface(Typeface.create("sans-serif-black", Typeface.NORMAL));
        play.setMinHeight(dp(56));
        addRow(gamePanel, edition, 12);
        addRow(gamePanel, language, 8);
        goldRides = new Switch(this);
        goldRides.setText(R.string.gold_rides);
        goldRides.setTextColor(PAPER);
        goldRides.setMinHeight(dp(48));
        goldRides.setPadding(dp(12), 0, dp(12), 0);
        goldRides.setVisibility(View.GONE);
        goldRides.setOnClickListener(view -> saveGoldRides());
        addRow(gamePanel, goldRides, 8);
        addRow(gamePanel, play, 14);

        dataPanel = panel(0xFF31554E, 0xFF203C40, 0xFF668B7F);
        TextView dataTitle = heading(getString(R.string.game_data), 25);
        addRow(dataPanel, dataTitle, 0);
        status = label("", 15, PAPER);
        status.setBackground(gradient(0xFF1C3937, 0xFF172E33, 0xFF55796C, 10, 1));
        status.setPadding(dp(12), dp(10), dp(12), dp(10));
        status.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        addRow(dataPanel, status, 8);
        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setIndeterminate(true);
        progress.setIndeterminateTintList(ColorStateList.valueOf(GOLD));
        addRow(dataPanel, progress, 6);
        files = launcherButton(R.string.import_files, ButtonStyle.IMPORT, () -> launchPicker(false));
        folder = launcherButton(R.string.import_folder, ButtonStyle.IMPORT, () -> launchPicker(true));
        addRow(dataPanel, files, 12);
        addRow(dataPanel, folder, 8);
        manageFiles = launcherButton(R.string.manage_files, ButtonStyle.NAVIGATION, this::showFileTools);
        addRow(dataPanel, manageFiles, 8);

        panels = new LinearLayout(this);
        panels.addView(gamePanel);
        panels.addView(dataPanel);
        addRow(body, panels, 12);
        arrangeScreen(getResources().getConfiguration());
    }

    private LinearLayout launcherTitle() {
        LinearLayout title = new LinearLayout(this);
        title.setGravity(Gravity.CENTER_VERTICAL);
        ImageView icon = new ImageView(this);
        icon.setImageResource(getApplicationInfo().icon);
        icon.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        LinearLayout.LayoutParams image = new LinearLayout.LayoutParams(dp(48), dp(48));
        image.setMarginEnd(dp(11));
        title.addView(icon, image);
        TextView name = heading(getString(R.string.launcher_name), 27);
        title.addView(name, new LinearLayout.LayoutParams(0, -2, 1));
        return title;
    }

    @Override
    public void onConfigurationChanged(Configuration configuration) {
        super.onConfigurationChanged(configuration);
        arrangeScreen(configuration);
    }

    private void play() {
        Intent game = new Intent(this, ThemeParkActivity.class);
        // Some Recents screens use the component's manifest label in the card menu.
        if (usEdition()) game.setClassName(getPackageName(), "org.tpwport.android.SimThemePark");
        startActivity(game.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            .putExtra(EDITION, chosenEdition).putExtra(LANGUAGE, chosenLanguage)
            .putExtra(INTERFACE_SCALE, interfaceScale())
            .putExtra(IMMERSIVE, getPreferences(MODE_PRIVATE).getBoolean(IMMERSIVE, true))
            .putExtra(FILL_CUTOUT, getPreferences(MODE_PRIVATE).getBoolean(FILL_CUTOUT, true))
            .putExtra(RETURN_TO_LAUNCHER, getPreferences(MODE_PRIVATE).getBoolean(RETURN_TO_LAUNCHER, false)));
        Log.i("TPWLauncher", "play: handed to game task (" + chosenEdition + ")");
    }

    private void arrangeScreen(Configuration configuration) {
        boolean wide = configuration.screenWidthDp >= 600 && configuration.fontScale <= 1.25f;
        boolean compact = configuration.screenHeightDp < (wide ? 420 : 550);
        body.getLayoutParams().width = configuration.screenWidthDp > 940 ? dp(940) : -1;
        body.setPadding(dp(compact ? 10 : 16), dp(compact ? 8 : 14), dp(compact ? 10 : 16), dp(20));
        header.setPadding(dp(14), dp(compact ? 8 : 12), dp(14), dp(compact ? 11 : 15));
        int orientation = wide ? LinearLayout.HORIZONTAL : LinearLayout.VERTICAL;
        header.setOrientation(orientation);
        header.setGravity(Gravity.CENTER_VERTICAL);
        header.getChildAt(0).setLayoutParams(new LinearLayout.LayoutParams(wide ? 0 : -1, -2, wide ? 1 : 0));
        LinearLayout.LayoutParams actions = new LinearLayout.LayoutParams(wide ? -2 : -1, -2);
        actions.topMargin = wide ? 0 : dp(8);
        actions.setMarginStart(wide ? dp(12) : 0);
        quickActions.setLayoutParams(actions);
        options.setLayoutParams(actionParams(0, wide));
        mods.setLayoutParams(actionParams(8, wide));

        panels.setOrientation(orientation);
        ((LinearLayout.LayoutParams) panels.getLayoutParams()).topMargin = dp(compact ? 8 : 12);
        gamePanel.setLayoutParams(new LinearLayout.LayoutParams(wide ? 0 : -1, wide ? -1 : -2, wide ? 11 : 0));
        LinearLayout.LayoutParams data = new LinearLayout.LayoutParams(wide ? 0 : -1, wide ? -1 : -2, wide ? 9 : 0);
        data.topMargin = wide ? 0 : dp(12);
        data.setMarginStart(wide ? dp(12) : 0);
        dataPanel.setLayoutParams(data);
        body.requestLayout();
    }

    private LinearLayout.LayoutParams actionParams(int start, boolean wrap) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(wrap ? -2 : 0, -2, wrap ? 0 : 1);
        params.setMarginStart(dp(start));
        return params;
    }

    private LinearLayout column() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        return layout;
    }

    private LinearLayout panel(int top, int bottom, int edge) {
        LinearLayout layout = column();
        layout.setBackground(frame(top, bottom, edge, 19));
        layout.setPadding(dp(16), dp(14), dp(16), dp(18));
        return layout;
    }

    private void addRow(LinearLayout parent, View child, int top) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1, -2);
        params.topMargin = dp(top);
        parent.addView(child, params);
    }

    private int dp(float value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private TextView label(String text, int size, int color) {
        TextView label = new TextView(this);
        label.setText(text);
        label.setTextColor(color);
        label.setTextSize(size);
        return label;
    }

    private TextView heading(String text, int size) {
        TextView title = label(text, size, PAPER);
        title.setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD));
        title.setAccessibilityHeading(true);
        return title;
    }

    private GradientDrawable gradient(int top, int bottom, int stroke, int radius, int width) {
        GradientDrawable shape = new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, new int[] {top, bottom});
        shape.setCornerRadius(dp(radius));
        if (width > 0) shape.setStroke(dp(width), stroke);
        return shape;
    }

    private LayerDrawable frame(int top, int bottom, int edge, int radius) {
        LayerDrawable frame = new LayerDrawable(new GradientDrawable[] {
            gradient(0xFF253E57, 0xFF081929, 0xFF0A1827, radius, 1),
            gradient(top, bottom, edge, radius - 4, 1)
        });
        frame.setLayerInset(1, dp(4), dp(4), dp(4), dp(6));
        return frame;
    }

    private void alignStart(Button button) {
        button.setGravity(Gravity.CENTER_VERTICAL | Gravity.START);
    }

    private Button launcherButton(int text, ButtonStyle style, Runnable action) {
        Button button = launcherButton(text, style);
        button.setOnClickListener(view -> action.run());
        return button;
    }

    private Button launcherButton(int text, ButtonStyle style) {
        Button button = new Button(this);
        button.setText(text);
        button.setAllCaps(false);
        button.setTextColor(new ColorStateList(new int[][] {{-android.R.attr.state_enabled}, {}},
            new int[] {0xFFB9C7BD, style.text}));
        button.setTextSize(16);
        button.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        button.setMinHeight(dp(48));
        button.setPadding(dp(14), dp(7), dp(14), dp(9));
        button.setStateListAnimator(null);
        StateListDrawable background = new StateListDrawable();
        background.addState(new int[] {-android.R.attr.state_enabled}, frame(0xFF394A48, 0xFF273835, 0xFF68756E, 12));
        background.addState(new int[] {android.R.attr.state_pressed}, frame(style.bottom, style.top, GOLD, 12));
        background.addState(new int[] {android.R.attr.state_focused}, frame(style.top, style.bottom, 0xFFFFF1BE, 12));
        background.addState(new int[0], frame(style.top, style.bottom, style.edge, 12));
        button.setBackground(background);
        return button;
    }

    private void showOptions(boolean showMods) {
        String root = getFilesDir().getAbsolutePath();
        String[] rows = nativeOptions(root, showMods, "", false);
        if (rows == null) {
            status.setText(R.string.options_unavailable);
            return;
        }
        ScrollView scroll = new ScrollView(this);
        LinearLayout fields = column();
        fields.setPadding(dp(18), dp(4), dp(18), dp(12));
        scroll.addView(fields);
        AlertDialog dialog = new AlertDialog.Builder(this).setTitle(showMods ? R.string.mods : R.string.settings)
            .setView(scroll).setPositiveButton(R.string.done, null).create();
        if (!showMods) {
            Button controls = launcherButton(R.string.controls, ButtonStyle.NAVIGATION, () -> {
                dialog.dismiss();
                new ControlSettings(this).show(this);
            });
            alignStart(controls);
            addRow(fields, controls, 4);
            Button scale = launcherButton(R.string.interface_scale, ButtonStyle.NAVIGATION);
            scale.setText(interfaceScaleLabel(interfaceScale()));
            scale.setOnClickListener(view -> chooseInterfaceScale(scale));
            alignStart(scale);
            addRow(fields, scale, 8);
            Button graphics = launcherButton(R.string.graphics_settings, ButtonStyle.NAVIGATION, () -> showChoiceOptions(root, false));
            alignStart(graphics);
            addRow(fields, graphics, 8);
            Button gameOptions = launcherButton(R.string.game_options, ButtonStyle.NAVIGATION,
                () -> showGameOptions(root, chosenEdition));
            gameOptions.setEnabled(!chosenEdition.isEmpty());
            alignStart(gameOptions);
            addRow(fields, gameOptions, 8);
            Button camera = launcherButton(R.string.camera_settings, ButtonStyle.NAVIGATION, () -> showChoiceOptions(root, true));
            alignStart(camera);
            addRow(fields, camera, 8);
            String[] server = nativeOnlineServer(root, null);
            if (server != null) {
                Button online = launcherButton(R.string.online_server, ButtonStyle.CHOICE);
                updateOnlineServerLabel(online, server);
                online.setOnClickListener(view -> showOnlineServer(root, online));
                alignStart(online);
                addRow(fields, online, 8);
            }
            addPreference(fields, R.string.immersive_full_screen, IMMERSIVE, true, null);
            addPreference(fields, R.string.fill_display_cutout, FILL_CUTOUT, true, null);
            addPreference(fields, R.string.return_to_launcher, RETURN_TO_LAUNCHER, false, null);
            addPreference(fields, R.string.auto_updates, AUTO_UPDATES, true, () -> {
                if (getPreferences(MODE_PRIVATE).getBoolean(AUTO_UPDATES, true)) checkForUpdates();
                else update.setVisibility(View.GONE);
            });
        }
        for (int i = 0; i + 5 < rows.length; i += 6) {
            final String key = rows[i];
            LinearLayout option = column();
            option.setBackground(gradient(0xFF234842, 0xFF1B3838, 0xFF496D65, 12, 1));
            option.setPadding(dp(12), 0, dp(12), 0);
            Switch toggle = optionSwitch();
            toggle.setText(showMods ? rows[i + 1] : settingLabel(key));
            toggle.setChecked(rows[i + 2].equals("1"));
            toggle.setEnabled(rows[i + 3].equals("1"));
            toggle.setOnClickListener(view -> {
                if (nativeOptions(root, showMods, key, toggle.isChecked()) != null) return;
                toggle.setChecked(!toggle.isChecked());
                android.widget.Toast.makeText(this, R.string.option_save_failed, android.widget.Toast.LENGTH_SHORT).show();
            });
            addRow(option, toggle, 0);
            if (!rows[i + 4].isEmpty()) {
                String note = rows[i + 4];
                int requiredDisc = note.equals("ps1") ? R.string.mod_need_ps1 :
                    note.equals("ps2") ? R.string.mod_need_ps2 : R.string.mod_need_tpi;
                String detail = toggle.isEnabled() ? note : getString(requiredDisc);
                TextView description = label(detail, 14, MUTED);
                description.setPadding(0, 0, 0, dp(9));
                addRow(option, description, 0);
            }
            String refresh = rows[i + 5];
            if (!refresh.isEmpty()) {
                addRow(option, label(getString(R.string.mod_outdated), 14, PAPER), 0);
                Button rebuild = launcherButton(R.string.refresh_mod_data, ButtonStyle.NAVIGATION, () -> {
                    dialog.dismiss();
                    showRefreshSources(refresh, true);
                });
                addRow(option, rebuild, 6);
            }
            addRow(fields, option, 8);
        }
        dialog.show();
    }

    private Switch optionSwitch() {
        Switch toggle = new Switch(this);
        toggle.setTextColor(new ColorStateList(new int[][] {{-android.R.attr.state_enabled}, {}},
            new int[] {MUTED, PAPER}));
        toggle.setTextSize(16);
        toggle.setMinHeight(dp(48));
        toggle.setPadding(0, dp(8), 0, dp(8));
        toggle.setSwitchPadding(dp(12));
        return toggle;
    }

    private void addPreference(LinearLayout fields, int title, String key, boolean defaultOn, Runnable changed) {
        SharedPreferences preferences = getPreferences(MODE_PRIVATE);
        Switch toggle = optionSwitch();
        toggle.setText(title);
        toggle.setChecked(preferences.getBoolean(key, defaultOn));
        toggle.setOnCheckedChangeListener((view, checked) -> {
            preferences.edit().putBoolean(key, checked).apply();
            if (changed != null) changed.run();
        });
        addRow(fields, toggle, 8);
    }

    private void checkForUpdates() {
        if (checkingUpdates || !getPreferences(MODE_PRIVATE).getBoolean(AUTO_UPDATES, true)) return;
        checkingUpdates = true;
        final String installedVersion = versionName();
        new Thread(() -> {
            AndroidUpdates.Release found = AndroidUpdates.newest(installedVersion, Build.SUPPORTED_ABIS);
            runOnUiThread(() -> {
                checkingUpdates = false;
                if (isFinishing() || isDestroyed() || found == null ||
                    !getPreferences(MODE_PRIVATE).getBoolean(AUTO_UPDATES, true)) return;
                update.setText(getString(R.string.update_available, found.version));
                update.setOnClickListener(view -> new AlertDialog.Builder(this).setTitle(R.string.download_update)
                    .setMessage(getString(R.string.update_install_prompt, found.version))
                    .setPositiveButton(R.string.download_update, (dialog, which) -> openReleases(found.download))
                    .setNegativeButton(R.string.cancel, null).show());
                update.setVisibility(View.VISIBLE);
            });
        }, "Launcher update check").start();
    }

    private void openReleases(String url) {
        try { startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url))); }
        catch (ActivityNotFoundException error) { status.setText(R.string.browser_unavailable); }
    }

    private void updateOnlineServerLabel(Button button, String[] server) {
        String value = server[1].isEmpty() ? getString(R.string.online_server_auto) : server[1];
        button.setText(getString(R.string.online_server_current, value));
    }

    private void showOnlineServer(String root, Button button) {
        String[] current = nativeOnlineServer(root, null);
        if (current == null) {
            status.setText(R.string.options_unavailable);
            return;
        }

        LinearLayout contents = column();
        contents.setPadding(dp(22), dp(8), dp(22), dp(12));
        EditText host = new EditText(this);
        host.setSingleLine(true);
        host.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        host.setContentDescription(getString(R.string.online_server));
        host.setHint(current[2]);
        host.setText(current[1]);
        host.setSelectAllOnFocus(true);
        addRow(contents, host, 0);
        addRow(contents, label(getString(R.string.online_server_hint, current[2]), 14, PAPER), 8);
        addRow(contents, label(getString(R.string.online_password_hint), 14, MUTED), 8);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(contents);

        AlertDialog dialog = new AlertDialog.Builder(this).setTitle(R.string.online_server).setView(scroll)
            .setPositiveButton(R.string.online_server_save, null).setNegativeButton(R.string.cancel, null).create();
        dialog.show();
        if (dialog.getWindow() != null) {
            dialog.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
        }
        dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view -> {
            String[] updated = nativeOnlineServer(root, host.getText().toString().trim());
            if (updated == null) host.setError(getString(R.string.option_save_failed));
            else if (updated[0].equals("invalid")) host.setError(getString(R.string.online_server_invalid));
            else {
                updateOnlineServerLabel(button, updated);
                dialog.dismiss();
            }
        });
    }

    private void showChoiceOptions(String root, boolean camera) {
        int maxFrameRate = 60;
        Display display = getDisplay();
        if (display != null) {
            for (Display.Mode mode : display.getSupportedModes()) {
                maxFrameRate = Math.max(maxFrameRate, Math.round(mode.getRefreshRate()));
            }
        }
        final int top = maxFrameRate;
        int heading = camera ? R.string.camera_settings : R.string.graphics_settings;
        String[] rows = nativeChoiceOptions(root, camera, "", -1, top);
        if (rows == null) {
            android.widget.Toast.makeText(this, R.string.options_unavailable, android.widget.Toast.LENGTH_SHORT).show();
            return;
        }
        ScrollView scroll = new ScrollView(this);
        LinearLayout fields = column();
        fields.setPadding(dp(18), dp(4), dp(18), dp(12));
        scroll.addView(fields);
        addRow(fields, label(getString(R.string.graphics_apply_on_launch), 14, MUTED), 4);
        for (int i = 0; i + 2 < rows.length; i += 3) {
            String key = rows[i];
            String title = settingLabel(key);
            String[] values = splitLines(rows[i + 2]);
            String[] choices = new String[values.length];
            for (int j = 0; j < values.length; ++j) choices[j] = graphicsValueLabel(values[j]);
            Button button = launcherButton(heading, ButtonStyle.CHOICE);
            updateGraphicsChoice(button, title, rows[i + 1]);
            alignStart(button);
            java.util.function.IntConsumer save = index -> {
                if (nativeChoiceOptions(root, camera, key, index, top) != null) {
                    updateGraphicsChoice(button, title, values[index]);
                } else {
                    android.widget.Toast.makeText(this, R.string.option_save_failed, android.widget.Toast.LENGTH_SHORT).show();
                }
            };
            button.setOnClickListener(view -> {
                int current = Arrays.asList(values).indexOf(button.getTag());
                if (key.equals("frame_rate_limit")) {
                    chooseRange(title, Math.max(current, 0), choices.length - 1, index -> choices[index], save);
                } else {
                    new AlertDialog.Builder(this).setTitle(title).setSingleChoiceItems(choices, current, (picker, index) -> {
                        picker.dismiss();
                        save.accept(index);
                    }).setNegativeButton(R.string.cancel, null).show();
                }
            });
            addRow(fields, button, 8);
        }
        new AlertDialog.Builder(this).setTitle(heading).setView(scroll)
            .setPositiveButton(R.string.done, null).show();
    }

    private void updateGraphicsChoice(Button button, String title, String value) {
        button.setTag(value);
        button.setText(getString(R.string.graphics_setting_value, title, graphicsValueLabel(value)));
    }

    private String graphicsValueLabel(String value) {
        switch (value) {
            case "Auto": return getString(R.string.graphics_auto);
            case "Default": return getString(R.string.graphics_default);
            case "Original": return getString(R.string.graphics_original);
            case "Farther": return getString(R.string.graphics_farther);
            case "Whole world": return getString(R.string.graphics_whole_world);
            case "Short": return getString(R.string.graphics_short);
            case "Med": return getString(R.string.graphics_medium);
            case "Long": return getString(R.string.graphics_long);
            case "Max": return getString(R.string.graphics_max);
            case "Off": return getString(R.string.graphics_off);
            case "On": return getString(R.string.graphics_on);
            case "Low": return getString(R.string.graphics_low);
            case "High": return getString(R.string.graphics_high);
            case "Moves the camera": return getString(R.string.free_swipe_move);
            case "Zooms and rotates": return getString(R.string.free_swipe_zoom);
            default: return value;
        }
    }

    private void showGameOptions(String root, String editionCode) {
        ScrollView scroll = new ScrollView(this);
        LinearLayout fields = column();
        fields.setPadding(dp(18), dp(4), dp(18), dp(12));
        scroll.addView(fields);
        if (!renderGameOptions(root, editionCode, "", -1, 0, scroll, fields)) return;
        new AlertDialog.Builder(this).setTitle(R.string.game_options).setView(scroll)
            .setPositiveButton(R.string.done, null).show();
    }

    private boolean renderGameOptions(String root, String editionCode, String profile, int option, int value,
                                      ScrollView scroll, LinearLayout fields) {
        Display display = getDisplay();
        Display.Mode mode = display == null ? null : display.getMode();
        int width = mode == null ? 1920 : mode.getPhysicalWidth();
        int height = mode == null ? 1080 : mode.getPhysicalHeight();
        if (option >= 0 && !canChangeFiles()) return false;
        String[] rows = nativeGameOptions(root, editionCode, profile, option, value, width, height);
        if (rows == null) {
            android.widget.Toast.makeText(this, option < 0 ? R.string.options_unavailable : R.string.option_save_failed,
                android.widget.Toast.LENGTH_SHORT).show();
            return false;
        }

        int scrollY = scroll.getScrollY();
        fields.removeAllViews();
        int count = Integer.parseInt(rows[0]);
        int current = Integer.parseInt(rows[1]);
        String[] profiles = Arrays.copyOfRange(rows, 3, 3 + count);
        String selected = current < 0 ? "" : profiles[current];
        int hint = rows[2].equals("running") ? R.string.quit_before_files :
            rows[2].equals("unavailable") ? R.string.options_unavailable : R.string.game_options_saved;
        addRow(fields, label(getString(hint), 14, MUTED), 4);
        Button player = launcherButton(R.string.game_option_profile, ButtonStyle.CHOICE);
        player.setText(getString(R.string.graphics_setting_value, getString(R.string.game_option_profile),
            current < 0 ? getString(R.string.game_option_no_profile) : selected));
        player.setEnabled(count > 1);
        alignStart(player);
        player.setOnClickListener(view -> new AlertDialog.Builder(this).setTitle(R.string.game_option_profile)
            .setSingleChoiceItems(profiles, current, (picker, index) -> {
                picker.dismiss();
                renderGameOptions(root, editionCode, profiles[index], -1, 0, scroll, fields);
            }).setNegativeButton(R.string.cancel, null).show());
        addRow(fields, player, 8);

        for (int i = 3 + count; i + 4 < rows.length; i += 5) {
            int id = Integer.parseInt(rows[i]);
            String title = getString(gameOptionLabel(rows[i + 1]));
            int currentValue = Integer.parseInt(rows[i + 2]);
            String kind = rows[i + 4];
            Button field = launcherButton(R.string.game_options, ButtonStyle.CHOICE);
            alignStart(field);
            field.setEnabled(rows[i + 3].equals("1"));
            if (kind.equals("percent")) {
                field.setText(getString(R.string.graphics_setting_value, title,
                    getString(R.string.interface_scale_percent, currentValue)));
                field.setOnClickListener(view -> chooseRange(title, currentValue, 100,
                    next -> getString(R.string.interface_scale_percent, next),
                    next -> renderGameOptions(root, editionCode, selected, id, next, scroll, fields)));
            } else {
                String[] choices;
                int[] values;
                if (rows[i + 1].equals("resolution")) {
                    String[] sizes = splitLines(kind);
                    choices = new String[sizes.length];
                    values = new int[sizes.length];
                    for (int j = 0; j < sizes.length; ++j) {
                        int divider = sizes[j].indexOf(':');
                        values[j] = Integer.parseInt(sizes[j].substring(0, divider));
                        choices[j] = sizes[j].substring(divider + 1);
                    }
                } else {
                    int[] labels = gameOptionChoices(kind);
                    values = new int[labels.length];
                    choices = new String[labels.length];
                    for (int j = 0; j < labels.length; ++j) {
                        values[j] = j;
                        choices[j] = getString(labels[j]);
                    }
                }
                int picked = -1;
                for (int j = 0; j < values.length; ++j) if (currentValue == values[j]) picked = j;
                final int initial = picked;
                field.setText(initial < 0 ? title : getString(R.string.graphics_setting_value, title, choices[initial]));
                field.setOnClickListener(view -> new AlertDialog.Builder(this).setTitle(title)
                    .setSingleChoiceItems(choices, initial, (picker, index) -> {
                        picker.dismiss();
                        renderGameOptions(root, editionCode, selected, id, values[index], scroll, fields);
                    }).setNegativeButton(R.string.cancel, null).show());
            }
            addRow(fields, field, 8);
        }
        scroll.post(() -> scroll.scrollTo(0, scrollY));
        return true;
    }

    private void chooseRange(String title, int value, int maximum, java.util.function.IntFunction<String> format,
                             java.util.function.IntConsumer save) {
        LinearLayout fields = column();
        fields.setPadding(dp(16), dp(4), dp(16), dp(8));
        TextView number = label(format.apply(value), 15, PAPER);
        addRow(fields, number, 4);
        SeekBar slider = new SeekBar(this);
        slider.setContentDescription(title);
        slider.setMax(maximum);
        slider.setProgress(value);
        slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
                number.setText(format.apply(progress));
            }
            @Override public void onStartTrackingTouch(SeekBar bar) { }
            @Override public void onStopTrackingTouch(SeekBar bar) { }
        });
        addRow(fields, slider, 6);
        new AlertDialog.Builder(this).setTitle(title).setView(fields)
            .setPositiveButton(R.string.online_server_save, (dialog, which) -> save.accept(slider.getProgress()))
            .setNegativeButton(R.string.cancel, null).show();
    }

    private static int[] gameOptionChoices(String kind) {
        switch (kind) {
            case "detail": return new int[] {R.string.graphics_low, R.string.graphics_medium, R.string.graphics_high};
            case "software_detail": return new int[] {R.string.graphics_low, R.string.graphics_medium};
            case "rendering": return new int[] {R.string.game_option_software, R.string.game_option_3d};
            case "rotation": return new int[] {R.string.game_option_smooth, R.string.game_option_quarter_turns};
            case "scroll": return new int[] {R.string.game_option_edge_scroll, R.string.right_mouse};
            default: return new int[] {R.string.graphics_off, R.string.graphics_on};
        }
    }

    private static int gameOptionLabel(String key) {
        switch (key) {
            case "resolution": return R.string.game_option_resolution;
            case "detail": return R.string.game_option_detail;
            case "rendering": return R.string.game_option_rendering;
            case "audio_quality": return R.string.game_option_audio_quality;
            case "movies": return R.string.game_option_movies;
            case "movie_volume": return R.string.game_option_movie_volume;
            case "sfx": return R.string.game_option_sfx;
            case "sfx_volume": return R.string.game_option_sfx_volume;
            case "music": return R.string.game_option_music;
            case "music_volume": return R.string.game_option_music_volume;
            case "speech": return R.string.game_option_speech;
            case "speech_volume": return R.string.game_option_speech_volume;
            case "advisor": return R.string.game_option_advisor;
            case "tutorial": return R.string.game_option_tutorial;
            case "popup_help": return R.string.game_option_popup_help;
            case "confirmations": return R.string.game_option_confirmations;
            case "rmb_cancel": return R.string.game_option_rmb_cancel;
            case "rotation": return R.string.game_option_rotation;
            case "scroll": return R.string.game_option_scroll;
            default: return R.string.game_options;
        }
    }

    private int interfaceScale() {
        return getPreferences(MODE_PRIVATE).getInt(INTERFACE_SCALE, 100);
    }

    private String interfaceScaleLabel(int percent) {
        return getString(R.string.interface_scale_value, percent);
    }

    private void chooseInterfaceScale(Button button) {
        String[] choices = new String[INTERFACE_SCALES.length];
        int selection = 0;
        int current = interfaceScale();
        for (int i = 0; i < choices.length; ++i) {
            choices[i] = getString(R.string.interface_scale_percent, INTERFACE_SCALES[i]);
            if (current == INTERFACE_SCALES[i]) selection = i;
        }
        new AlertDialog.Builder(this).setTitle(R.string.interface_scale)
            .setSingleChoiceItems(choices, selection, (dialog, index) -> {
                int percent = INTERFACE_SCALES[index];
                getPreferences(MODE_PRIVATE).edit().putInt(INTERFACE_SCALE, percent).apply();
                button.setText(interfaceScaleLabel(percent));
                dialog.dismiss();
            }).setNegativeButton(R.string.cancel, null).show();
    }

    private String settingLabel(String key) {
        switch (key) {
            case "smooth_wheel_zoom": return getString(R.string.smooth_zoom);
            case "far_zoom": return getString(R.string.far_zoom);
            case "pc_sound_picks": return getString(R.string.sound_variation);
            case "sound_panning": return getString(R.string.sound_pan);
            case "ride_badges": return getString(R.string.ride_badges);
            case "wasd_camera": return getString(R.string.wasd_camera);
            case "several_keys": return getString(R.string.several_keys);
            case "free_camera_look_sensitivity": return getString(R.string.free_look_sensitivity);
            case "free_camera_fly_speed": return getString(R.string.free_fly_speed);
            case "free_camera_invert_look": return getString(R.string.free_invert_look);
            case "free_camera_smoothing": return getString(R.string.free_smoothing);
            case "free_camera_through_ground": return getString(R.string.free_through_ground);
            case "free_camera_swipe": return getString(R.string.free_swipe);
            case "skip_intro": return getString(R.string.skip_intro);
            case "frame_rate_limit": return getString(R.string.graphics_frame_rate);
            case "draw_distance": return getString(R.string.graphics_draw_distance);
            case "sam_view_distance": return getString(R.string.graphics_view_distance);
            case "sam_sky_quality": return getString(R.string.graphics_sky_layers);
            case "sam_sky_shadow": return getString(R.string.graphics_cloud_shadows);
            case "sam_procedural_texturing": return getString(R.string.graphics_ground_blending);
            case "sam_texture_quality": return getString(R.string.graphics_texture_quality);
            case "sam_lighting": return getString(R.string.graphics_lighting);
            case "sam_sprite_shadow": return getString(R.string.graphics_visitor_shadows);
            case "sam_num_kids": return getString(R.string.graphics_visitor_looks);
            default: return key;
        }
    }

    private void launchPicker(boolean tree) {
        launchImportPicker(tree, tree ? PICK_FOLDER : PICK_FILES);
    }

    private void launchImportPicker(boolean tree, int request) {
        if (!canChangeFiles()) return;
        Intent intent = new Intent(tree ? Intent.ACTION_OPEN_DOCUMENT_TREE : Intent.ACTION_OPEN_DOCUMENT);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        if (!tree) intent.setType("*/*").addCategory(Intent.CATEGORY_OPENABLE).putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        launchDocument(intent, request);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null) return;
        // Android can recreate the launcher while the picker is open. Wait for the initial scan in that case.
        if (busy) pendingDocument = () -> handleDocument(request, data);
        else handleDocument(request, data);
    }

    private void handleDocument(int request, Intent data) {
        if (request >= PICK_PACK && request <= EXPORT_LOG) {
            if (data.getData() != null) runFileTool(request, data.getData());
            return;
        }
        boolean tree = request == PICK_FOLDER || request == REFRESH_PS1_FOLDER || request == REFRESH_TPI_FOLDER;
        String refresh = request == REFRESH_PS1_FILES || request == REFRESH_PS1_FOLDER ? "ps1" :
                         request == REFRESH_TPI_FILES || request == REFRESH_TPI_FOLDER ? "tpi" : "";
        if ((request != PICK_FILES && request != PICK_FOLDER && refresh.isEmpty()) || !canChangeFiles()) return;
        List<Uri> sources = new ArrayList<>();
        ClipData clip = data.getClipData();
        if (clip != null) {
            for (int i = 0; i < clip.getItemCount(); ++i) {
                Uri uri = clip.getItemAt(i).getUri();
                if (uri != null && !sources.contains(uri)) sources.add(uri);
            }
        } else if (data.getData() != null) sources.add(data.getData());
        if (!sources.isEmpty()) runImport(sources, tree, refresh);
    }

    private void showRefreshSources(String mod, boolean allowPrevious) {
        if (!canChangeFiles()) return;
        SharedPreferences preferences = getPreferences(MODE_PRIVATE);
        List<Uri> previous = new ArrayList<>();
        for (String value : splitLines(preferences.getString("mod_sources_" + mod, ""))) previous.add(Uri.parse(value));
        boolean remembered = allowPrevious && !previous.isEmpty() && previous.stream().allMatch(uri ->
            getContentResolver().getPersistedUriPermissions().stream().anyMatch(
                permission -> permission.isReadPermission() && permission.getUri().equals(uri)));
        String[] choices = remembered ? new String[]{getString(R.string.refresh_previous), getString(R.string.import_files),
            getString(R.string.import_folder)} : new String[]{getString(R.string.import_files), getString(R.string.import_folder)};
        new AlertDialog.Builder(this).setTitle(R.string.refresh_mod_data).setItems(choices, (dialog, index) -> {
            if (remembered && index == 0) {
                runImport(previous, preferences.getBoolean("mod_source_tree_" + mod, false), mod);
            } else {
                boolean tree = index == choices.length - 1;
                int request = mod.equals("ps1") ? REFRESH_PS1_FILES : REFRESH_TPI_FILES;
                launchImportPicker(tree, request + (tree ? 1 : 0));
            }
        }).setNegativeButton(R.string.cancel, null).show();
    }

    private void rememberModSources(String mod, List<Uri> sources, boolean tree) {
        List<String> saved = new ArrayList<>();
        try {
            for (Uri uri : sources) {
                getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                saved.add(uri.toString());
            }
        } catch (SecurityException ignored) {
            return;   // A provider without a durable grant can be picked again when needed.
        }
        getPreferences(MODE_PRIVATE).edit().putString("mod_sources_" + mod, String.join("\n", saved))
            .putBoolean("mod_source_tree_" + mod, tree).apply();
    }

    private void runImport(List<Uri> sources, boolean tree, String refresh) {
        setBusy(true);
        status.setText(sources.isEmpty() ? R.string.import_checking : R.string.import_copying);
        work.execute(() -> {
            Path stage = null;
            String[] result;
            try {
                System.loadLibrary("tpw_launcher");
                List<String> paths = new ArrayList<>();
                if (!sources.isEmpty()) {
                    stage = Files.createTempDirectory(getCacheDir().toPath(), "import-");
                    for (Uri uri : sources) {
                        if (tree) {
                            copyFolder(uri, DocumentsContract.getTreeDocumentId(uri), stage, 0);
                            paths.add(stage.toString());
                        } else {
                            Path file = stage.resolve(safeName(displayName(uri)));
                            copyFile(uri, file);
                            paths.add(file.toString());
                        }
                    }
                    runOnUiThread(() -> status.setText(R.string.import_verifying));
                }
                if (!gameRunning()) {
                    SaveArchive.recoverSaves(saveFolder(true));
                    SaveArchive.recoverSaves(saveFolder(false));
                }
                result = nativeImport(getFilesDir().getAbsolutePath(), paths.toArray(new String[0]), READ_DATA, "");
            } catch (IOException | RuntimeException | LinkageError error) {
                android.util.Log.e("TPWImport", "Import failed", error);
                result = null;
            } finally {
                deleteStage(stage);
            }
            final String[] outcome = result;
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed()) return;
                boolean ps1 = outcome != null && outcome[11].equals("1");
                boolean tpi = outcome != null && outcome[12].equals("1");
                if (ps1) rememberModSources("ps1", sources, tree);
                if (tpi) rememberModSources("tpi", sources, tree);
                showResult(outcome, !sources.isEmpty());
                if (!refresh.isEmpty()) {
                    boolean converted = refresh.equals("ps1") ? ps1 : tpi;
                    status.setText(converted ? R.string.mod_refresh_done : R.string.mod_refresh_failed);
                    if (!converted) showRefreshSources(refresh, false);
                }
            });
        });
    }

    private String displayName(Uri uri) throws IOException {
        try (Cursor cursor = getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor == null || !cursor.moveToFirst()) throw new IOException("Document name unavailable");
            return cursor.getString(0);
        }
    }

    private static String safeName(String name) throws IOException {
        if (name == null || name.isEmpty() || name.equals(".") || name.equals("..") || name.indexOf('/') >= 0 ||
            name.indexOf('\\') >= 0 || name.indexOf('\0') >= 0) throw new IOException("Invalid document name");
        return name;
    }

    private void copyFile(Uri uri, Path file) throws IOException {
        try (InputStream stream = getContentResolver().openInputStream(uri)) {
            if (stream == null) throw new IOException("Document stream unavailable");
            Files.copy(stream, file);
        }
    }

    private void copyFolder(Uri tree, String folderId, Path destination, int depth) throws IOException {
        if (depth >= 32) throw new IOException("Folder nested too deeply");
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, folderId);
        String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                            DocumentsContract.Document.COLUMN_MIME_TYPE};
        try (Cursor cursor = getContentResolver().query(children, columns, null, null, null)) {
            if (cursor == null) throw new IOException("Folder is unavailable");
            while (cursor.moveToNext()) {
                String id = cursor.getString(0);
                Path target = destination.resolve(safeName(cursor.getString(1)));
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(2))) {
                    Files.createDirectory(target);
                    copyFolder(tree, id, target, depth + 1);
                } else copyFile(DocumentsContract.buildDocumentUriUsingTree(tree, id), target);
            }
        }
    }

    private static void deleteStage(Path stage) {
        if (stage == null) return;
        try (Stream<Path> paths = Files.walk(stage)) {
            paths.sorted(Comparator.reverseOrder()).forEach(path -> {
                try { Files.deleteIfExists(path); }
                catch (IOException error) { android.util.Log.w("TPWImport", "Staging cleanup failed", error); }
            });
        } catch (IOException error) { android.util.Log.w("TPWImport", "Staging cleanup failed", error); }
    }

    private void setBusy(boolean running) {
        busy = running;
        progress.setVisibility(running ? View.VISIBLE : View.GONE);
        files.setEnabled(!running);
        folder.setEnabled(!running);
        manageFiles.setEnabled(!running);
        options.setEnabled(!running);
        mods.setEnabled(!running);
        play.setEnabled(!running && !chosenEdition.isEmpty());
        edition.setEnabled(!running && editions.length > 1);
        language.setEnabled(!running && !chosenEdition.isEmpty() && languages().length > 1);
        goldRides.setEnabled(!running && goldAvailable && !chosenEdition.isEmpty());
    }

    private void showResult(String[] result, boolean imported) {
        if (isFinishing() || isDestroyed()) return;
        if (result != null) {
            editions = split(result[1]);
            europeanLanguages = splitLines(result[2]);
            americanLanguages = splitLines(result[4]);
            europeanLabels = splitLines(result[5]);
            americanLabels = splitLines(result[6]);
            goldAvailable = !result[7].isEmpty();
            goldUs = result[7].equals("1");
            goldEur = result[8].equals("1");
            installed = splitLines(result[9]);
            String oldEdition = getPreferences(MODE_PRIVATE).getString(EDITION, "");
            chosenEdition = contains(editions, oldEdition) ? oldEdition : editions.length == 0 ? "" : editions[0];
            chooseSavedLanguage();
        }
        updateLabels();
        setBusy(false);
        if (result != null && result[0].equals("ps2_import_error")) {
            status.setText(R.string.ps2_import_error);
        } else if (result == null || result[0].endsWith("error")) {
            status.setText(R.string.import_failed);
        } else if (result[0].equals("ps2_ready")) {
            status.setText(R.string.ps2_import_ready);
        } else if (result[0].equals("unrecognized")) {
            status.setText(R.string.import_unrecognized);
        } else if (result[0].equals("mod_source_incomplete")) {
            status.setText(R.string.mod_source_incomplete);
        } else if (result[11].equals("1") || result[12].equals("1")) {
            status.setText(R.string.mod_content_ready);
        } else if (!result[13].equals("0")) {
            status.setText(getString(R.string.corrupt_data_removed, result[13]));
        } else if (chosenEdition.isEmpty()) {
            status.setText(imported ? R.string.import_need_base : R.string.import_prompt);
        } else {
            status.setText(imported ? getString(R.string.data_ready_import, result[3]) : getString(R.string.data_ready));
        }
        Runnable document = pendingDocument;
        pendingDocument = null;
        if (document != null) document.run();
        else if (imported && result != null) nameTranslations(splitLines(result[10]), 0);
    }

    private void nameTranslations(String[] translations, int index) {
        if (index >= translations.length || !canChangeFiles()) return;
        String[] names = nativeTranslationLanguages();
        if (names == null || names.length == 0) return;
        new AlertDialog.Builder(this).setTitle(R.string.name_translation).setItems(names, (dialog, choice) -> {
            if (!canChangeFiles()) return;
            setBusy(true);
            work.execute(() -> {
                boolean named = nativeNameTranslation(getFilesDir().getAbsolutePath(), translations[index], names[choice]);
                String[] updated = nativeImport(getFilesDir().getAbsolutePath(), new String[0], READ_DATA, "");
                runOnUiThread(() -> {
                    if (isFinishing() || isDestroyed()) return;
                    showResult(updated, false);
                    if (!named) status.setText(R.string.name_translation_failed);
                    nameTranslations(translations, index + 1);
                });
            });
        }).setNegativeButton(R.string.cancel, null).show();
    }

    private boolean gameRunning() {
        List<ActivityManager.RunningAppProcessInfo> processes = getSystemService(ActivityManager.class).getRunningAppProcesses();
        if (processes == null) return false;
        String game = getPackageName() + ":game";
        for (ActivityManager.RunningAppProcessInfo process : processes) {
            if (game.equals(process.processName)) return true;
        }
        return false;
    }

    private boolean canChangeFiles() {
        if (busy) return false;
        if (!gameRunning()) return true;
        status.setText(R.string.quit_before_files);
        return false;
    }

    private void showFileTools() {
        int[] actions = { R.string.installed_editions, R.string.data_file_details, R.string.asset_packs, R.string.import_saves,
            R.string.export_saves, R.string.diagnostics };
        String[] labels = Arrays.stream(actions).mapToObj(this::getString).toArray(String[]::new);
        new AlertDialog.Builder(this).setTitle(R.string.manage_files).setItems(labels, (dialog, index) -> {
            switch (actions[index]) {
                case R.string.installed_editions: showInstalledEditions(); break;
                case R.string.data_file_details:
                    String[] codes = {"us", "us-pc", "eur", "eur-pc", "gold"};
                    String[] titles = Arrays.stream(codes).map(this::editionLabel).toArray(String[]::new);
                    new AlertDialog.Builder(this).setTitle(R.string.data_file_details).setItems(titles,
                        (picker, which) -> showEditionDetails(codes[which], null))
                        .setNegativeButton(R.string.cancel, null).show();
                    break;
                case R.string.asset_packs: showAssetPacks(); break;
                case R.string.import_saves: chooseSaveEdition(true); break;
                case R.string.export_saves: chooseSaveEdition(false); break;
                case R.string.diagnostics: showDiagnostics(); break;
                default: break;
            }
        }).setNegativeButton(R.string.done, null).show();
    }

    private void showInstalledEditions() {
        AlertDialog.Builder dialog = new AlertDialog.Builder(this).setTitle(R.string.installed_editions)
            .setNegativeButton(R.string.done, null);
        if (installed.length == 0) {
            dialog.setMessage(R.string.no_installed_editions).show();
            return;
        }
        String[] labels = new String[installed.length];
        for (int i = 0; i < installed.length; ++i) {
            String[] row = installed[i].split(":");
            labels[i] = getString(R.string.installed_edition_summary, editionLabel(row[0]), row[1], row[2]);
        }
        dialog.setItems(labels, (list, index) -> {
            if (!canChangeFiles()) return;
            String code = installed[index].split(":")[0];
            String title = editionLabel(code);
            new AlertDialog.Builder(this).setTitle(title).setItems(new String[]{getString(R.string.data_file_details),
                getString(R.string.verify_edition),
                getString(R.string.remove_edition)}, (options, action) -> {
                if (action == 0) showEditionDetails(code, null);
                else if (action == 1) changeEditionData(VERIFY_DATA, code);
                else new AlertDialog.Builder(this).setTitle(R.string.remove_edition)
                    .setMessage(getString(R.string.remove_edition_confirm, title))
                    .setNegativeButton(R.string.cancel, null)
                    .setPositiveButton(R.string.remove_edition, (confirm, button) -> changeEditionData(REMOVE_DATA, code)).show();
            }).setNegativeButton(R.string.cancel, null).show();
        }).show();
    }

    private void showEditionDetails(String code, String restore) {
        if (!canChangeFiles()) return;
        setBusy(true);
        status.setText(R.string.file_operation_progress);
        work.execute(() -> {
            String[] rows = nativeEditionDetails(getFilesDir().getAbsolutePath(), code, restore);
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed()) return;
                setBusy(false);
                if (rows == null) {
                    status.setText(R.string.file_operation_failed);
                    return;
                }
                status.setText(restore != null ? R.string.original_restored :
                    chosenEdition.isEmpty() ? R.string.import_prompt : R.string.data_ready);
                AlertDialog.Builder details = new AlertDialog.Builder(this).setTitle(editionLabel(code))
                    .setNegativeButton(R.string.done, null);
                if (rows.length == 0) {
                    details.setMessage(R.string.no_data_file_problems).show();
                    return;
                }
                String[] labels = new String[rows.length / 4];
                for (int i = 0; i < labels.length; ++i) {
                    int row = i * 4;
                    String info;
                    if (rows[row].equals("gap")) {
                        info = getString(rows[row + 3].equals("1") ? R.string.data_file_required : R.string.data_file_optional);
                        if (!rows[row + 2].isEmpty()) info += " · " + rows[row + 2];
                    } else if (rows[row + 2].equals("repaired")) {
                        info = getString(R.string.data_file_repaired);
                    } else if (!rows[row + 2].equals("0")) {
                        info = getString(R.string.data_file_unreadable, rows[row + 2]);
                    } else {
                        info = getString(R.string.data_file_customized);
                    }
                    labels[i] = rows[row + 1] + "\n" + info;
                }
                details.setItems(labels, (list, index) -> {
                    int row = index * 4;
                    String path = rows[row + 1];
                    AlertDialog.Builder item = new AlertDialog.Builder(this).setTitle(path)
                        .setNegativeButton(R.string.cancel, null);
                    if (rows[row].equals("gap") || !rows[row + 3].equals("1")) {
                        item.setMessage(rows[row].equals("gap") ? labels[index] : getString(R.string.original_needs_disc))
                            .setPositiveButton(R.string.import_files, (dialog, which) -> launchPicker(false))
                            .setNeutralButton(R.string.import_folder, (dialog, which) -> launchPicker(true));
                    } else {
                        item.setMessage(R.string.restore_original_confirm)
                            .setPositiveButton(R.string.restore_original, (dialog, which) -> showEditionDetails(code, path));
                    }
                    item.show();
                }).show();
            });
        });
    }

    private void changeEditionData(int operation, String code) {
        if (!canChangeFiles()) return;
        setBusy(true);
        status.setText(R.string.file_operation_progress);
        work.execute(() -> {
            String[] result = nativeImport(getFilesDir().getAbsolutePath(), new String[0], operation, code);
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed()) return;
                showResult(result, false);
                if (result == null || !result[0].equals("ok")) status.setText(R.string.file_operation_failed);
                else status.setText(operation == VERIFY_DATA ? getString(R.string.edition_verified, result[3])
                    : getString(R.string.edition_removed));
            });
        });
    }

    private void showAssetPacks() {
        if (!canChangeFiles()) return;
        try {
            List<String> names = AndroidFiles.assetPacks(getFilesDir().toPath());
            AlertDialog.Builder dialog = new AlertDialog.Builder(this).setTitle(R.string.asset_packs)
                .setPositiveButton(R.string.import_asset_pack, (picker, button) -> openZip(PICK_PACK))
                .setNegativeButton(R.string.done, null);
            if (names.isEmpty()) dialog.setMessage(R.string.no_asset_packs);
            else dialog.setItems(names.toArray(new String[0]), (picker, index) -> {
                String name = names.get(index);
                new AlertDialog.Builder(this).setTitle(R.string.remove_asset_pack).setMessage(name)
                    .setNegativeButton(R.string.cancel, null).setPositiveButton(R.string.remove_asset_pack, (confirm, button) -> {
                        if (!canChangeFiles()) return;
                        try {
                            Files.deleteIfExists(getFilesDir().toPath().resolve("packs").resolve(name));
                            status.setText(R.string.asset_pack_removed);
                        } catch (IOException error) {
                            Log.e("TPWFiles", "Could not remove asset pack", error);
                            status.setText(R.string.file_operation_failed);
                        }
                    }).show();
            });
            dialog.show();
        } catch (IOException error) {
            Log.e("TPWFiles", "Could not list asset packs", error);
            status.setText(R.string.file_operation_failed);
        }
    }

    private Path saveFolder(boolean us) {
        return getFilesDir().toPath().resolve("saves").resolve(us ? "us" : "eur");
    }

    private void chooseSaveEdition(boolean restore) {
        if (!canChangeFiles()) return;
        int title = restore ? R.string.import_saves : R.string.export_saves;
        String[] names = {getString(R.string.game_name_us), getString(R.string.game_name_europe)};
        new AlertDialog.Builder(this).setTitle(title).setItems(names, (dialog, index) -> {
            boolean us = index == 0;
            if (restore) {
                new AlertDialog.Builder(this).setTitle(title).setMessage(getString(R.string.restore_saves_confirm, names[index]))
                    .setNegativeButton(R.string.cancel, null)
                    .setPositiveButton(title, (confirm, button) -> openZip(us ? PICK_SAVES_US : PICK_SAVES_EUR)).show();
            } else if (Files.isDirectory(saveFolder(us).resolve("save"))) {
                createDocument(us ? EXPORT_SAVES_US : EXPORT_SAVES_EUR, "application/zip", names[index] + " - saves.zip");
            } else status.setText(R.string.no_saves);
        }).setNegativeButton(R.string.cancel, null).show();
    }

    private void openZip(int request) {
        if (!canChangeFiles()) return;
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE);
        launchDocument(intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION), request);
    }

    private void createDocument(int request, String type, String name) {
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT).setType(type).addCategory(Intent.CATEGORY_OPENABLE)
            .putExtra(Intent.EXTRA_TITLE, name).addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        launchDocument(intent, request);
    }

    private void launchDocument(Intent intent, int request) {
        try { startActivityForResult(intent, request); }
        catch (ActivityNotFoundException error) { status.setText(R.string.picker_unavailable); }
    }

    private void runFileTool(int request, Uri document) {
        if (busy || (request != EXPORT_LOG && !canChangeFiles())) return;
        setBusy(true);
        status.setText(R.string.file_operation_progress);
        work.execute(() -> {
            Path stage = null;
            int message = R.string.file_operation_failed;
            try {
                stage = Files.createTempDirectory(getCacheDir().toPath(), "files-");
                if (request == PICK_PACK) {
                    Path zip = stage.resolve(safeName(displayName(document)));
                    copyFile(document, zip);
                    AndroidFiles.installPack(getFilesDir().toPath(), zip, stage);
                    message = R.string.asset_pack_added;
                } else if (request == PICK_SAVES_US || request == PICK_SAVES_EUR) {
                    Path zip = stage.resolve("saves.zip");
                    copyFile(document, zip);
                    SaveArchive.restoreSaves(saveFolder(request == PICK_SAVES_US), zip, stage);
                    message = R.string.saves_imported;
                } else if (request == EXPORT_LOG) {
                    AndroidFiles.copyToDocument(this, AndroidFiles.exportLog(this, stage), document);
                    message = R.string.log_exported;
                } else {
                    Path zip = stage.resolve("saves.zip");
                    SaveArchive.exportSaves(saveFolder(request == EXPORT_SAVES_US), zip);
                    AndroidFiles.copyToDocument(this, zip, document);
                    message = R.string.saves_exported;
                }
            } catch (IOException | RuntimeException error) {
                Log.e("TPWFiles", "File operation failed", error);
                if (request == PICK_PACK) message = R.string.asset_pack_failed;
                else if (request == PICK_SAVES_US || request == PICK_SAVES_EUR) message = R.string.saves_import_failed;
            } finally {
                deleteStage(stage);
            }
            int outcome = message;
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed()) return;
                setBusy(false);
                status.setText(outcome);
            });
        });
    }

    private String versionName() {
        try { return getPackageManager().getPackageInfo(getPackageName(), 0).versionName; }
        catch (PackageManager.NameNotFoundException ignored) { return ""; }
    }

    private void showDiagnostics() {
        int[] actions = {R.string.show_log, R.string.export_log, R.string.licenses, R.string.check_updates};
        String[] titles = Arrays.stream(actions).mapToObj(this::getString).toArray(String[]::new);
        new AlertDialog.Builder(this).setTitle(getString(R.string.diagnostics_version, versionName())).setItems(titles, (dialog, i) -> {
            switch (actions[i]) {
                case R.string.show_log: showLog(); break;
                case R.string.export_log: createDocument(EXPORT_LOG, "text/plain", "STP-TPW Launcher logs.txt"); break;
                case R.string.licenses: showLicenses(); break;
                case R.string.check_updates: openReleases(AndroidUpdates.RELEASES); break;
                default: break;
            }
        }).setNegativeButton(R.string.done, null).show();
    }

    private void showLog() {
        setBusy(true);
        work.execute(() -> {
            Path stage = null;
            String preview = null;
            try {
                stage = Files.createTempDirectory(getCacheDir().toPath(), "log-");
                preview = AndroidFiles.previewLog(this, stage);
            } catch (IOException | RuntimeException error) {
                Log.e("TPWFiles", "Could not read log", error);
            } finally {
                deleteStage(stage);
            }
            String log = preview;
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed()) return;
                setBusy(false);
                if (log == null) status.setText(R.string.log_unavailable);
                else showFileText(getString(R.string.show_log), log);
            });
        });
    }

    private void showLicenses() {
        try {
            String[] names = getAssets().list("licenses");
            if (names == null) return;
            new AlertDialog.Builder(this).setTitle(R.string.licenses).setItems(names, (dialog, index) -> {
                try (InputStream in = getAssets().open("licenses/" + names[index])) {
                    ByteArrayOutputStream contents = new ByteArrayOutputStream();
                    byte[] buffer = new byte[4096];
                    for (int read; (read = in.read(buffer)) != -1;) contents.write(buffer, 0, read);
                    showFileText(names[index], new String(contents.toByteArray(), StandardCharsets.UTF_8));
                } catch (IOException error) { status.setText(R.string.file_operation_failed); }
            }).setNegativeButton(R.string.done, null).show();
        } catch (IOException error) { status.setText(R.string.file_operation_failed); }
    }

    private void showFileText(String title, String content) {
        ScrollView view = new ScrollView(this);
        TextView text = new TextView(this);
        text.setText(content);
        text.setTextSize(13);
        text.setPadding(dp(16), dp(10), dp(16), dp(12));
        text.setTextIsSelectable(true);
        view.addView(text);
        new AlertDialog.Builder(this).setTitle(title).setView(view).setPositiveButton(R.string.done, null).show();
    }

    private void choose(boolean voice) {
        String[] keys = voice ? languages() : editions;
        String[] labels = new String[keys.length];
        for (int i = 0; i < keys.length; ++i) labels[i] = voice ? languageLabel(keys[i]) : editionLabel(keys[i]);
        new AlertDialog.Builder(this).setTitle(voice ? R.string.game_language : R.string.game_edition).setItems(labels, (dialog, index) -> {
            if (voice) {
                chosenLanguage = keys[index];
                getPreferences(MODE_PRIVATE).edit().putString(languageKey(), chosenLanguage).apply();
            } else {
                chosenEdition = keys[index];
                getPreferences(MODE_PRIVATE).edit().putString(EDITION, chosenEdition).apply();
                chooseSavedLanguage();
                setBusy(false);
            }
            updateLabels();
        }).setNegativeButton(R.string.cancel, null).show();
    }

    private boolean usEdition() { return chosenEdition.startsWith("us"); }

    private String[] languages() { return usEdition() ? americanLanguages : europeanLanguages; }

    private String languageKey() { return usEdition() ? "language_us" : "language_eur"; }

    private void migrateLanguagePreference() {
        SharedPreferences preferences = getPreferences(MODE_PRIVATE);
        if (!preferences.contains(LANGUAGE)) return;
        boolean us = preferences.getString(EDITION, "").startsWith("us");
        String from = preferences.getString(LANGUAGE, "");
        String key = us ? "language_us" : "language_eur";
        SharedPreferences.Editor editor = preferences.edit().remove(LANGUAGE);
        if (!preferences.contains(key)) editor.putString(key, from.isEmpty() ? (us ? "American" : "English") : from);
        editor.apply();
    }

    private void chooseSavedLanguage() {
        String saved = getPreferences(MODE_PRIVATE).getString(languageKey(), "auto");
        String[] choices = languages();
        boolean preferPolish = getResources().getConfiguration().getLocales().get(0).getLanguage().equals("pl");
        if (saved.equals("auto") && preferPolish) saved = "Polish";
        chosenLanguage = chosenEdition.isEmpty() || choices.length == 0 ? "" : contains(choices, saved) ? saved : choices[0];
    }

    private void updateLabels() {
        boolean pc = chosenEdition.endsWith("-pc");
        gameTitle.setText(chosenEdition.isEmpty() ? getString(R.string.select_edition) :
            usEdition() ? "Sim Theme Park" : "Theme Park World");
        String region = getString(usEdition() ? R.string.region_us : R.string.region_europe);
        edition.setText(chosenEdition.isEmpty() ? getString(R.string.edition_empty) :
            getString(R.string.edition_current, pc ? "PC" : "Mac", region));
        language.setText(getString(R.string.language_current, languageLabel(chosenLanguage)));
        goldRides.setVisibility(goldAvailable && !chosenEdition.isEmpty() ? View.VISIBLE : View.GONE);
        goldRides.setChecked(usEdition() ? goldUs : goldEur);
    }

    private void saveGoldRides() {
        boolean enabled = goldRides.isChecked();
        String key = usEdition() ? "gold_us" : "gold_eur";
        if (nativeOptions(getFilesDir().getAbsolutePath(), false, key, enabled) == null) {
            goldRides.setChecked(!enabled);
            android.widget.Toast.makeText(this, R.string.option_save_failed, android.widget.Toast.LENGTH_SHORT).show();
        } else if (usEdition()) {
            goldUs = enabled;
        } else {
            goldEur = enabled;
        }
    }

    private String editionLabel(String code) {
        switch (code) {
            case "us": return "Sim Theme Park";
            case "us-pc": return "Sim Theme Park (PC)";
            case "eur": return "Theme Park World";
            case "eur-pc": return "Theme Park World (PC)";
            case "gold": return getString(R.string.gold_rides);
            default: return code;
        }
    }

    private String languageLabel(String code) {
        if (code.isEmpty()) return getString(R.string.language_unavailable);
        String[] names = languages();
        String[] labels = usEdition() ? americanLabels : europeanLabels;
        for (int i = 0; i < names.length; ++i) {
            if (!names[i].equals(code)) continue;
            String shown = labels[i];
            if (shown.equals("English")) return getString(R.string.language_english);
            if (shown.equals("American English")) return getString(R.string.language_american);
            if (shown.equals("Polski")) return getString(R.string.language_polish);
            return shown;
        }
        return code;
    }

    private static String[] split(String list) { return list.isEmpty() ? new String[0] : list.split(","); }
    private static String[] splitLines(String list) { return list.isEmpty() ? new String[0] : list.split("\\n"); }

    private static boolean contains(String[] options, String item) {
        for (String option : options) if (option.equals(item)) return true;
        return false;
    }

    @Override
    protected void onDestroy() {
        work.shutdown();
        super.onDestroy();
    }
}

package org.tpwport.android;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.ColorStateList;
import android.content.res.Configuration;
import android.database.Cursor;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.LayerDrawable;
import android.graphics.drawable.StateListDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.stream.Stream;

public final class SetupActivity extends Activity {
    static final String EDITION = "edition";
    static final String LANGUAGE = "language";
    static final String INTERFACE_SCALE = "interface_scale";
    private static final int[] INTERFACE_SCALES = {100, 115, 125};
    private static final int PICK_FILES = 1;
    private static final int PICK_FOLDER = 2;
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
    private Button play, files, folder, edition, language, options, mods;
    private Switch goldRides;
    private TextView gameTitle, status;
    private LinearLayout body, header, quickActions, panels, gamePanel, dataPanel;
    private ProgressBar progress;
    private boolean busy;
    private String[] editions = {}, europeanLanguages = {}, americanLanguages = {};
    private String[] europeanLabels = {}, americanLabels = {};
    private String chosenEdition = "", chosenLanguage = "";
    private boolean goldAvailable, goldUs, goldEur;

    private static native String[] nativeImport(String dataFolder, String[] inputs);
    private static native String[] nativeOptions(String dataFolder, boolean mods, String changedKey, boolean enabled);

    @Override
    protected void onCreate(Bundle previous) {
        super.onCreate(previous);
        setTaskDescription(new ActivityManager.TaskDescription.Builder()
            .setLabel(getString(R.string.launcher_name)).setIcon(R.mipmap.tpw_icon).build());
        migrateLanguagePreference();
        createScreen();
        runImport(new ArrayList<>(), false);
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
            .putExtra(INTERFACE_SCALE, interfaceScale()));
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
        }
        for (int i = 0; i + 4 < rows.length; i += 5) {
            final String key = rows[i];
            LinearLayout option = column();
            option.setBackground(gradient(0xFF234842, 0xFF1B3838, 0xFF496D65, 12, 1));
            option.setPadding(dp(12), 0, dp(12), 0);
            Switch toggle = new Switch(this);
            toggle.setText(showMods ? rows[i + 1] : settingLabel(key));
            toggle.setTextColor(new ColorStateList(new int[][] {{-android.R.attr.state_enabled}, {}},
                new int[] {MUTED, PAPER}));
            toggle.setTextSize(16);
            toggle.setChecked(rows[i + 2].equals("1"));
            toggle.setEnabled(rows[i + 3].equals("1"));
            toggle.setMinHeight(dp(48));
            toggle.setPadding(0, dp(8), 0, dp(8));
            toggle.setSwitchPadding(dp(12));
            toggle.setOnClickListener(view -> {
                if (nativeOptions(root, showMods, key, toggle.isChecked()) != null) return;
                toggle.setChecked(!toggle.isChecked());
                android.widget.Toast.makeText(this, R.string.option_save_failed, android.widget.Toast.LENGTH_SHORT).show();
            });
            addRow(option, toggle, 0);
            if (!rows[i + 4].isEmpty()) {
                String note = rows[i + 4];
                String detail = toggle.isEnabled() ? note : getString(note.equals("ps1") ? R.string.mod_need_ps1 : R.string.mod_need_tpi);
                TextView description = label(detail, 14, MUTED);
                description.setPadding(0, 0, 0, dp(9));
                addRow(option, description, 0);
            }
            addRow(fields, option, 8);
        }
        dialog.show();
    }

    private int interfaceScale() {
        return getPreferences(MODE_PRIVATE).getInt(INTERFACE_SCALE, INTERFACE_SCALES[0]);
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
            default: return key;
        }
    }

    private void launchPicker(boolean tree) {
        if (busy) return;
        Intent intent = new Intent(tree ? Intent.ACTION_OPEN_DOCUMENT_TREE : Intent.ACTION_OPEN_DOCUMENT);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        if (!tree) intent.setType("*/*").addCategory(Intent.CATEGORY_OPENABLE).putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        try { startActivityForResult(intent, tree ? PICK_FOLDER : PICK_FILES); }
        catch (android.content.ActivityNotFoundException error) {
            status.setText(R.string.picker_unavailable);
        }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || (request != PICK_FILES && request != PICK_FOLDER)) return;
        List<Uri> sources = new ArrayList<>();
        ClipData clip = data.getClipData();
        if (clip != null) {
            for (int i = 0; i < clip.getItemCount(); ++i) {
                Uri uri = clip.getItemAt(i).getUri();
                if (uri != null && !sources.contains(uri)) sources.add(uri);
            }
        } else if (data.getData() != null) sources.add(data.getData());
        if (!sources.isEmpty()) runImport(sources, request == PICK_FOLDER);
    }

    private void runImport(List<Uri> sources, boolean tree) {
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
                result = nativeImport(getFilesDir().getAbsolutePath(), paths.toArray(new String[0]));
            } catch (IOException | RuntimeException | LinkageError error) {
                android.util.Log.e("TPWImport", "Import failed", error);
                result = null;
            } finally {
                deleteStage(stage);
            }
            final String[] outcome = result;
            runOnUiThread(() -> showResult(outcome, !sources.isEmpty()));
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
            String oldEdition = getPreferences(MODE_PRIVATE).getString(EDITION, "");
            chosenEdition = contains(editions, oldEdition) ? oldEdition : editions.length == 0 ? "" : editions[0];
            chooseSavedLanguage();
        }
        updateLabels();
        setBusy(false);
        if (result == null || result[0].endsWith("error")) {
            status.setText(R.string.import_failed);
        } else if (result[0].equals("unrecognized")) {
            status.setText(R.string.import_unrecognized);
        } else if (chosenEdition.isEmpty()) {
            status.setText(imported ? R.string.import_need_base : R.string.import_prompt);
        } else {
            status.setText(imported ? getString(R.string.data_ready_import, result[3]) : getString(R.string.data_ready));
        }
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

    private static String editionLabel(String code) {
        switch (code) {
            case "us": return "Sim Theme Park";
            case "us-pc": return "Sim Theme Park (PC)";
            case "eur": return "Theme Park World";
            case "eur-pc": return "Theme Park World (PC)";
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

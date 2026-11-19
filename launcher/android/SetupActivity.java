package org.tpwport.android;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.database.Cursor;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
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
    private static final int INK = 0xFF2B203C;
    private static final int CREAM = 0xFFFFF9E8;
    private static final int ORANGE = 0xFFFFB35D;
    private static final int YELLOW = 0xFFFFE27E;
    private static final int LIME = 0xFFB6E763;
    private final ExecutorService work = Executors.newSingleThreadExecutor();
    private Button play, files, folder, edition, language, options, mods;
    private TextView gameTitle, status;
    private LinearLayout header, quickActions, panels, gamePanel, dataPanel;
    private ProgressBar progress;
    private boolean busy;
    private String[] editions = {}, europeanLanguages = {}, americanLanguages = {};
    private String[] europeanLabels = {}, americanLabels = {};
    private String chosenEdition = "", chosenLanguage = "";

    private static native String[] nativeImport(String dataFolder, String[] inputs);
    private static native String[] nativeOptions(String dataFolder, boolean mods, String changedKey, boolean enabled);

    @Override
    protected void onCreate(Bundle previous) {
        super.onCreate(previous);
        migrateLanguagePreference();
        createScreen();
        runImport(new ArrayList<>(), false);
    }

    private void createScreen() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackground(new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,
            new int[] {0xFF94D1F4, 0xFFE6F8FF}));
        LinearLayout body = column();
        body.setPadding(dp(16), dp(16), dp(16), dp(24));
        scroll.addView(body);
        setContentView(scroll);

        header = new LinearLayout(this);
        header.addView(launcherTitle());
        quickActions = new LinearLayout(this);
        options = launcherButton(R.string.settings, CREAM, () -> showOptions(false));
        mods = launcherButton(R.string.mods, YELLOW, () -> showOptions(true));
        quickActions.addView(options);
        quickActions.addView(mods);
        header.addView(quickActions);
        body.addView(header);

        gamePanel = panel(YELLOW);
        gameTitle = label(getString(R.string.select_edition), 29, true);
        gameTitle.setAccessibilityHeading(true);
        addRow(gamePanel, gameTitle, 0);
        edition = launcherButton(R.string.edition, CREAM, () -> choose(false));
        language = launcherButton(R.string.language, CREAM, () -> choose(true));
        play = launcherButton(R.string.play, LIME, () -> startActivity(new Intent(this, ThemeParkActivity.class)
            .putExtra(EDITION, chosenEdition).putExtra(LANGUAGE, chosenLanguage)
            .putExtra(INTERFACE_SCALE, interfaceScale())));
        play.setTextSize(20);
        play.setMinHeight(dp(54));
        addRow(gamePanel, edition, 18);
        addRow(gamePanel, language, 8);
        addRow(gamePanel, play, 14);

        dataPanel = panel(CREAM);
        TextView dataTitle = label(getString(R.string.game_data), 24, true);
        dataTitle.setAccessibilityHeading(true);
        addRow(dataPanel, dataTitle, 0);
        status = label("", 16, false);
        status.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        addRow(dataPanel, status, 8);
        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setIndeterminate(true);
        addRow(dataPanel, progress, 6);
        files = launcherButton(R.string.import_files, ORANGE, () -> launchPicker(false));
        folder = launcherButton(R.string.import_folder, ORANGE, () -> launchPicker(true));
        addRow(dataPanel, files, 18);
        addRow(dataPanel, folder, 8);

        panels = new LinearLayout(this);
        panels.addView(gamePanel);
        panels.addView(dataPanel);
        addRow(body, panels, 16);
        arrangeScreen(getResources().getConfiguration());
    }

    private LinearLayout launcherTitle() {
        LinearLayout title = new LinearLayout(this);
        title.setGravity(Gravity.CENTER_VERTICAL);
        ImageView icon = new ImageView(this);
        icon.setImageResource(getApplicationInfo().icon);
        LinearLayout.LayoutParams image = new LinearLayout.LayoutParams(dp(46), dp(46));
        image.setMarginEnd(dp(10));
        title.addView(icon, image);
        TextView name = label("STP/TPW Launcher", 24, true);
        name.setAccessibilityHeading(true);
        title.addView(name, new LinearLayout.LayoutParams(0, -2, 1));
        return title;
    }

    @Override
    public void onConfigurationChanged(Configuration configuration) {
        super.onConfigurationChanged(configuration);
        arrangeScreen(configuration);
    }

    private void arrangeScreen(Configuration configuration) {
        boolean wide = configuration.screenWidthDp >= 600;
        int orientation = wide ? LinearLayout.HORIZONTAL : LinearLayout.VERTICAL;
        header.setOrientation(orientation);
        header.setGravity(Gravity.CENTER_VERTICAL);
        header.getChildAt(0).setLayoutParams(new LinearLayout.LayoutParams(wide ? 0 : -1, -2, wide ? 1 : 0));
        LinearLayout.LayoutParams actions = new LinearLayout.LayoutParams(wide ? -2 : -1, -2);
        actions.topMargin = wide ? 0 : dp(12);
        quickActions.setLayoutParams(actions);
        options.setLayoutParams(actionParams(0, wide));
        mods.setLayoutParams(actionParams(8, wide));

        panels.setOrientation(orientation);
        gamePanel.setLayoutParams(new LinearLayout.LayoutParams(wide ? 0 : -1, -2, wide ? 11 : 0));
        LinearLayout.LayoutParams data = new LinearLayout.LayoutParams(wide ? 0 : -1, -2, wide ? 9 : 0);
        data.topMargin = wide ? 0 : dp(12);
        data.setMarginStart(wide ? dp(12) : 0);
        dataPanel.setLayoutParams(data);
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

    private LinearLayout panel(int fill) {
        LinearLayout layout = column();
        layout.setBackground(shape(fill, INK, 18, 2));
        layout.setPadding(dp(18), dp(18), dp(18), dp(18));
        layout.setElevation(dp(3));
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

    private TextView label(String text, int size, boolean heading) {
        TextView label = new TextView(this);
        label.setText(text);
        label.setTextColor(INK);
        label.setTextSize(size);
        if (heading) label.setTypeface(Typeface.create("sans-serif", Typeface.BOLD));
        return label;
    }

    private GradientDrawable shape(int fill, int stroke, int radius, int width) {
        GradientDrawable shape = new GradientDrawable();
        shape.setColor(fill);
        shape.setCornerRadius(dp(radius));
        shape.setStroke(dp(width), stroke);
        return shape;
    }

    private Button launcherButton(int text, int fill, Runnable action) {
        Button button = new Button(this);
        button.setText(text);
        button.setAllCaps(false);
        button.setTextColor(INK);
        button.setTextSize(16);
        button.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        button.setMinHeight(dp(48));
        button.setPadding(dp(14), dp(8), dp(14), dp(8));
        button.setStateListAnimator(null);
        StateListDrawable background = new StateListDrawable();
        background.addState(new int[] {-android.R.attr.state_enabled}, shape(0xFFE0DCCB, 0xFF938A94, 12, 1));
        background.addState(new int[] {android.R.attr.state_pressed}, shape(0xFFFFC66C, INK, 12, 2));
        background.addState(new int[] {android.R.attr.state_focused}, shape(fill, 0xFF24598D, 12, 3));
        background.addState(new int[0], shape(fill, INK, 12, 1));
        button.setBackground(background);
        button.setOnClickListener(view -> action.run());
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
        LinearLayout fields = new LinearLayout(this);
        fields.setOrientation(LinearLayout.VERTICAL);
        int margin = Math.round(22 * getResources().getDisplayMetrics().density);
        fields.setPadding(margin, margin / 2, margin, margin / 2);
        scroll.addView(fields);
        AlertDialog dialog = new AlertDialog.Builder(this).setTitle(showMods ? R.string.mods : R.string.settings)
            .setView(scroll).setPositiveButton(R.string.done, null).create();
        if (!showMods) {
            button(fields, R.string.controls, () -> {
                dialog.dismiss();
                new ControlSettings(this).show(this);
            });
            Button scale = new Button(this);
            scale.setAllCaps(false);
            scale.setText(interfaceScaleLabel(interfaceScale()));
            scale.setOnClickListener(view -> chooseInterfaceScale(scale));
            fields.addView(scale);
        }
        for (int i = 0; i + 4 < rows.length; i += 5) {
            final String key = rows[i];
            Switch toggle = new Switch(this);
            toggle.setText(showMods ? rows[i + 1] : settingLabel(key));
            toggle.setChecked(rows[i + 2].equals("1"));
            toggle.setEnabled(rows[i + 3].equals("1"));
            toggle.setMinHeight(margin * 2);
            toggle.setOnClickListener(view -> {
                if (nativeOptions(root, showMods, key, toggle.isChecked()) != null) return;
                toggle.setChecked(!toggle.isChecked());
                android.widget.Toast.makeText(this, R.string.option_save_failed, android.widget.Toast.LENGTH_SHORT).show();
            });
            fields.addView(toggle);
            if (!rows[i + 4].isEmpty()) {
                TextView detail = new TextView(this);
                String note = rows[i + 4];
                detail.setText(toggle.isEnabled() ? note : getString(note.equals("ps1") ? R.string.mod_need_ps1 : R.string.mod_need_tpi));
                fields.addView(detail);
            }
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

    private Button button(LinearLayout body, int title, Runnable action) {
        Button button = new Button(this);
        button.setText(title);
        button.setAllCaps(false);
        button.setOnClickListener(view -> action.run());
        body.addView(button);
        return button;
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
    }

    private void showResult(String[] result, boolean imported) {
        if (isFinishing() || isDestroyed()) return;
        if (result != null) {
            editions = split(result[1]);
            europeanLanguages = splitLines(result[2]);
            americanLanguages = splitLines(result[4]);
            europeanLabels = splitLines(result[5]);
            americanLabels = splitLines(result[6]);
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
    }

    private static String editionLabel(String code) {
        switch (code) {
            case "eur-pc": return "Theme Park World (PC)";
            case "us-pc": return "Sim Theme Park (PC)";
            case "eur": return "Theme Park World";
            case "us": return "Sim Theme Park";
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

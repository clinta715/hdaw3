# File Browser Directory Persistence & Default Directories Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remember the last-browsed directory in the file browser tree and add per-purpose default directory settings in Preferences.

**Architecture:** Three new QSettings keys for default directories (project, audio, MIDI) + one for last browser dir. The file browser tree restores its root from last-browsed → default audio dir → filesystem root. QFileDialog calls fall back from last-used → purpose-specific default → empty string. Preferences gets a new "Default Directories" group box.

**Tech Stack:** Qt 6 (QSettings, QFileDialog, QFileSystemModel, QLineEdit, QPushButton)

---

### Task 1: Add key constants and static accessors to PreferencesDialog.h

**Files:**
- Modify: `src/ui/PreferencesDialog.h:35-48`

- [ ] **Add four new key constants after line 37 (`kKeyRecentProjects`)**

Old:
```cpp
static inline constexpr auto kKeyRecentProjects = "recentProjects";
```

New:
```cpp
static inline constexpr auto kKeyRecentProjects = "recentProjects";
static inline constexpr auto kKeyDefaultProjectDir = "defaultProjectDirectory";
static inline constexpr auto kKeyDefaultAudioDir = "defaultAudioDirectory";
static inline constexpr auto kKeyDefaultMidiDir = "defaultMidiDirectory";
static inline constexpr auto kKeyLastBrowserDir = "lastBrowserDirectory";
```

- [ ] **Add three static accessor declarations** after the `setPianoRollSnapDivision` line (before the key constants):

```cpp
static QString getDefaultProjectDir();
static QString getDefaultAudioDir();
static QString getDefaultMidiDir();
```

- [ ] **Commit**

```bash
git add src/ui/PreferencesDialog.h
git commit -m "PreferencesDialog: add directory persistence key constants and accessors"
```

### Task 2: Add "Default Directories" group box to PreferencesDialog.cpp

**Files:**
- Modify: `src/ui/PreferencesDialog.cpp:96` (before `mainLayout->addStretch()`)

- [ ] **Add the Default Directories group box** between the MCP section and the stretch, before line 97:

```cpp
    // Default Directories section
    auto* dirsGroup = new QGroupBox("Default Directories", this);
    auto* dirsLayout = new QFormLayout(dirsGroup);

    auto makeDirRow = [&](const QString& label, QLineEdit*& edit, const QString& key) {
        auto* row = new QWidget(dirsGroup);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        edit = new QLineEdit(row);
        edit->setPlaceholderText("(not set)");
        edit->setReadOnly(true);
        rowLayout->addWidget(edit, 1);

        auto* browseBtn = new QPushButton("Browse...", row);
        auto* clearBtn = new QPushButton("Clear", row);
        rowLayout->addWidget(browseBtn);
        rowLayout->addWidget(clearBtn);

        connect(browseBtn, &QPushButton::clicked, this, [this, edit]() {
            QString dir = QFileDialog::getExistingDirectory(this, "Choose Directory",
                edit->text().isEmpty() ? QDir::homePath() : edit->text());
            if (!dir.isEmpty())
                edit->setText(QDir::toNativeSeparators(dir));
        });
        connect(clearBtn, &QPushButton::clicked, this, [edit]() {
            edit->clear();
        });

        return row;
    };

    QLineEdit* defaultProjectDirEdit = nullptr;
    QLineEdit* defaultAudioDirEdit = nullptr;
    QLineEdit* defaultMidiDirEdit = nullptr;

    dirsLayout->addRow("Project folder:", makeDirRow("Project folder", defaultProjectDirEdit, kKeyDefaultProjectDir));
    dirsLayout->addRow("Audio samples:", makeDirRow("Audio samples", defaultAudioDirEdit, kKeyDefaultAudioDir));
    dirsLayout->addRow("MIDI files:", makeDirRow("MIDI files", defaultMidiDirEdit, kKeyDefaultMidiDir));

    mainLayout->addWidget(dirsGroup);
```

Now add member pointers for the three line edits in the class (in the private section of PreferencesDialog.h, after `countInBarsSpin` on line 88):
```cpp
    QLineEdit* defaultProjectDirEdit = nullptr;
    QLineEdit* defaultAudioDirEdit = nullptr;
    QLineEdit* defaultMidiDirEdit = nullptr;
```

- [ ] **Include QDir and QFileDialog** at the top of PreferencesDialog.cpp (add after `#include <QGroupBox>`):
```cpp
#include <QDir>
#include <QFileDialog>
```

- [ ] **Save the three dir edits in onApply()** — add inside `onApply()` after the existing settings, before `emit preferencesApplied()`:
```cpp
    settings.setValue(kKeyDefaultProjectDir, defaultProjectDirEdit->text());
    settings.setValue(kKeyDefaultAudioDir, defaultAudioDirEdit->text());
    settings.setValue(kKeyDefaultMidiDir, defaultMidiDirEdit->text());
```

- [ ] **Load the three dir edits in loadSettings()** — add inside `loadSettings()` after the existing settings loads:
```cpp
    defaultProjectDirEdit->setText(settings.value(kKeyDefaultProjectDir).toString());
    defaultAudioDirEdit->setText(settings.value(kKeyDefaultAudioDir).toString());
    defaultMidiDirEdit->setText(settings.value(kKeyDefaultMidiDir).toString());
```

- [ ] **Add the three static accessor implementations** at the bottom of the file, after `setPianoRollSnapDivision`:

```cpp
QString PreferencesDialog::getDefaultProjectDir()
{
    auto& settings = PreferencesDialog::settings();
    return settings.value(kKeyDefaultProjectDir).toString();
}

QString PreferencesDialog::getDefaultAudioDir()
{
    auto& settings = PreferencesDialog::settings();
    return settings.value(kKeyDefaultAudioDir).toString();
}

QString PreferencesDialog::getDefaultMidiDir()
{
    auto& settings = PreferencesDialog::settings();
    return settings.value(kKeyDefaultMidiDir).toString();
}
```

- [ ] **Commit**

```bash
git add src/ui/PreferencesDialog.h src/ui/PreferencesDialog.cpp
git commit -m "PreferencesDialog: add Default Directories section with three path pickers"
```

### Task 3: Persist last browsed directory in ProjectPoolBrowser

**Files:**
- Modify: `src/ui/ProjectPoolBrowser.h:20-21`
- Modify: `src/ui/ProjectPoolBrowser.cpp:11-19`, `21`, `33-60`

- [ ] **Add `saveBrowsedDir()` method and current root tracking** to ProjectPoolBrowser.h — add after `setupUI()` in the private section:

```cpp
    void saveBrowsedDir() const;
    QString currentRootDir;
```

- [ ] **Update constructor** in ProjectPoolBrowser.cpp — after `setupUI()` at end of constructor, add:

```cpp
    // Restore last browsed directory
    auto& settings = PreferencesDialog::settings();
    currentRootDir = settings.value(PreferencesDialog::kKeyLastBrowserDir).toString();
    if (currentRootDir.isEmpty())
        currentRootDir = PreferencesDialog::getDefaultAudioDir();
    if (!currentRootDir.isEmpty())
    {
        fsModel->setRootPath(currentRootDir);
        fileTree->setRootIndex(fsModel->index(currentRootDir));
    }

    // Save current directory on activation/click
    connect(fileTree, &QTreeView::clicked, this, [this](const QModelIndex& idx) {
        if (fsModel->isDir(idx))
        {
            currentRootDir = fsModel->filePath(idx);
            saveBrowsedDir();
        }
    });
```

- [ ] **Replace `~ProjectPoolBrowser() = default`** with an implementation that saves the last browsed dir:

```cpp
ProjectPoolBrowser::~ProjectPoolBrowser()
{
    saveBrowsedDir();
}
```

- [ ] **Add `saveBrowsedDir()` method** — add after `refreshPool()`:

```cpp
void ProjectPoolBrowser::saveBrowsedDir() const
{
    if (currentRootDir.isEmpty())
        return;
    auto& settings = PreferencesDialog::settings();
    settings.setValue(PreferencesDialog::kKeyLastBrowserDir, currentRootDir);
}
```

- [ ] **Update the "Add File to Pool" dialog** in `setupUI()` (line 86-87) to use default audio dir fallback:

Old:
```cpp
        QString file = QFileDialog::getOpenFileName(this, "Import Audio",
            settings.value(PreferencesDialog::kKeyLastProjectDir).toString(),
```

New:
```cpp
        auto fileDir = settings.value(PreferencesDialog::kKeyLastProjectDir).toString();
        if (fileDir.isEmpty())
            fileDir = PreferencesDialog::getDefaultAudioDir();
        QString file = QFileDialog::getOpenFileName(this, "Import Audio",
            fileDir,
```

- [ ] **Commit**

```bash
git add src/ui/ProjectPoolBrowser.h src/ui/ProjectPoolBrowser.cpp
git commit -m "ProjectPoolBrowser: persist last browsed directory, default audio dir fallback"
```

### Task 4: Add default-dir fallback to QFileDialog calls in MainWindow.cpp

**Files:**
- Modify: `src/ui/MainWindow.cpp:758-761`, `858-861`, `1122-1125`, `1140-1143`

- [ ] **onOpen()** (line 758-761) — add default project dir fallback:

Old:
```cpp
    auto& settings = PreferencesDialog::settings();
    auto path = QFileDialog::getOpenFileName(this, "Open Project",
        settings.value(PreferencesDialog::kKeyLastProjectDir).toString(),
        "HDAW Projects (*.hdaw)");
```

New:
```cpp
    auto& settings = PreferencesDialog::settings();
    auto openDir = settings.value(PreferencesDialog::kKeyLastProjectDir).toString();
    if (openDir.isEmpty())
        openDir = PreferencesDialog::getDefaultProjectDir();
    auto path = QFileDialog::getOpenFileName(this, "Open Project",
        openDir,
        "HDAW Projects (*.hdaw)");
```

- [ ] **onSaveAs()** (line 858-861) — add default project dir fallback:

Old:
```cpp
    auto& settings = PreferencesDialog::settings();
    auto path = QFileDialog::getSaveFileName(this, "Save Project As",
        settings.value(PreferencesDialog::kKeyLastProjectDir).toString(),
        "HDAW Projects (*.hdaw)");
```

New:
```cpp
    auto& settings = PreferencesDialog::settings();
    auto saveDir = settings.value(PreferencesDialog::kKeyLastProjectDir).toString();
    if (saveDir.isEmpty())
        saveDir = PreferencesDialog::getDefaultProjectDir();
    auto path = QFileDialog::getSaveFileName(this, "Save Project As",
        saveDir,
        "HDAW Projects (*.hdaw)");
```

- [ ] **onImportAudio()** (line 1122-1125) — add default audio dir fallback:

Old:
```cpp
    auto& settings = PreferencesDialog::settings();
    auto path = QFileDialog::getOpenFileName(this, "Import Audio",
        settings.value(PreferencesDialog::kKeyLastProjectDir).toString(),
        "Audio Files (*.wav *.aiff *.aif *.mp3 *.flac *.ogg)");
```

New:
```cpp
    auto& settings = PreferencesDialog::settings();
    auto audioDir = settings.value(PreferencesDialog::kKeyLastProjectDir).toString();
    if (audioDir.isEmpty())
        audioDir = PreferencesDialog::getDefaultAudioDir();
    auto path = QFileDialog::getOpenFileName(this, "Import Audio",
        audioDir,
        "Audio Files (*.wav *.aiff *.aif *.mp3 *.flac *.ogg)");
```

- [ ] **onImportMIDI()** (line 1140-1143) — add default MIDI dir fallback:

Old:
```cpp
    auto& settings = PreferencesDialog::settings();
    auto path = QFileDialog::getOpenFileName(this, "Import MIDI",
        settings.value(PreferencesDialog::kKeyLastProjectDir).toString(),
        "MIDI Files (*.mid *.midi)");
```

New:
```cpp
    auto& settings = PreferencesDialog::settings();
    auto midiDir = settings.value(PreferencesDialog::kKeyLastProjectDir).toString();
    if (midiDir.isEmpty())
        midiDir = PreferencesDialog::getDefaultMidiDir();
    auto path = QFileDialog::getOpenFileName(this, "Import MIDI",
        midiDir,
        "MIDI Files (*.mid *.midi)");
```

- [ ] **Commit**

```bash
git add src/ui/MainWindow.cpp
git commit -m "MainWindow: add default-dir fallback for Open/Save/Import dialogs"
```

### Task 5: Build and verify

- [ ] **Build the project**

```bash
cmake --build build --config Debug
```

Expected: No errors. Any warnings should be pre-existing (TransportManager.h deprecated atomics, unreferenced parameters).

- [ ] **Run tests**

```bash
ctest --test-dir build -C Debug --output-on-failure
```

Expected: All tests pass.

- [ ] **Final commit** (if any build fixes needed)

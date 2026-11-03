# Saves

**English** | [简体中文](saves.zh-CN.md)

## Where saves live

The active save directory is `work/save/` inside the project, not inside the app bundle. It has the same layout as the game's console save data (`slot_00/` … `slot_05/`, `option.sav`, album, picture book and `storage/`).

When the game mounts the save, the host clones it to `work/save.work/`. Each time the game commits, the complete working copy is published back to `work/save/` with an APFS clone and a directory swap, so `work/save/` always holds a complete, consistent snapshot. An uncommitted working copy left behind by a crash is discarded on the next mount. Writers hold the exclusive lock `work/save.lock`. The implementation is in `host/filesystem.c`.

> [!WARNING]
> Changes made to `work/save/` while the game is running are **not** merged; the game's next commit replaces them. Quit the game before you copy, import or edit saves, and do not point sync tools at this directory.

## Back up

Quit the game, then copy the whole directory, for example:

```sh
mkdir -p work/save-backups
cp -Rc work/save "work/save-backups/$(date +%Y%m%d-%H%M%S)"
```

`cp -c` makes an APFS clone, which is instant and takes no extra space until files change.

## Import a save from your console

1. Export the *Tears of the Kingdom* save data from your own console with a save manager.
2. Quit the game and back up `work/save/` as shown above.
3. Keep the original export untouched, for example in `local/save-imports/` (ignored by Git).
4. Replace the contents of `work/save/` with a copy of the exported save directory.

Saves from earlier game versions load in v1.4.3; the game upgrades them itself. If you replace only a `progress.sav`, the matching caption and thumbnail stay old, so the save list may show the wrong place name until the game saves again.

## iPhone / iPad

`./script/ios.sh transfer seed` copies the Mac save to the device once. The transfer refuses to overwrite a different save that already exists on the device. Subsequent cross-device updates require cloud save sync below.

## Cloud saves on Mac, iPhone and iPad

Choose the same dedicated cloud directory using **Zelda-Mobile → 云存档 → 关联云存档文件夹…** on Mac and **Link Cloud Folder** on iOS. For example, create a folder in iCloud Drive. Do not select the active `work/save/`, any parent containing it, or a directory inside it. System folder pickers grant access and store bookmarks; no CloudKit container or additional account credentials are needed.

Mac pulls before starting the game, waiting up to five seconds. Cancel from the cloud menu to use local saves. Failed or timed-out downloads also leave local saves in use. Downloads never replace a mounted save. Switching to another app, choosing the upload menu item, or quitting normally uploads the last committed snapshot. Quit waits at most five seconds; unfinished uploads retain local saves. Force quit cannot guarantee an upload. iOS uploads when entering the background.

Both platforms share the A/B snapshot, content-hash and metadata protocol. Only fully verified downloads replace local saves; previous local versions go to `work/cloud-save-backups/`. Different nonempty saves on first linking, or changes on both devices, preserve both versions and report a conflict. Quit and back up both before resolving it. Unlinking does not delete cloud saves.

Save in-game and allow the cloud provider to finish network sync before switching devices. Avoid simultaneous play. A successful sync status means the local provider accepted the snapshot, not that another device has downloaded it.

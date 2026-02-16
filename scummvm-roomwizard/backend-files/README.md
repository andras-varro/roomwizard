# ScummVM RoomWizard Backend Files

This directory contains version-controlled source files for the RoomWizard backend implementation.

## Purpose

Instead of using a tar.gz archive, these files are now directly version-controlled in the roomwizard repository. This provides:

- **Better tracking**: See file-by-file changes in git history
- **Easier diffs**: Compare changes between versions
- **Simpler merges**: Resolve conflicts at the file level
- **Direct editing**: Modify files here and sync to ScummVM

## Files

### Backend Implementation
- `roomwizard.cpp/h` - Main backend implementation
- `roomwizard-graphics.cpp/h` - Graphics manager (framebuffer)
- `roomwizard-events.cpp/h` - Event source (touch input)
- `module.mk` - Build configuration

### Configuration
- `configure.patch` - Patch for ScummVM's configure script to add RoomWizard backend

### Documentation
- `IMPLEMENTATION_SUCCESS.md` - Implementation notes (if exists)

## Workflow

### Syncing Changes TO Version Control

After making changes in the ScummVM repository:

```bash
cd scummvm-roomwizard
./manage-scummvm-changes.sh sync
```

This copies the current backend files from `../scummvm/backends/platform/roomwizard/` to this directory and updates the configure patch.

### Restoring Changes FROM Version Control

To restore backend files to the ScummVM repository:

```bash
cd scummvm-roomwizard
./manage-scummvm-changes.sh restore
```

This copies files from this directory back to `../scummvm/backends/platform/roomwizard/` and applies the configure patch.

### Updating ScummVM from Upstream

To pull latest ScummVM changes while preserving your backend:

```bash
cd scummvm-roomwizard
./manage-scummvm-changes.sh update
```

This automatically syncs before and after the update.

## Migration from tar.gz

If you have an existing `roomwizard-backend.tar.gz` backup, you can:

1. Extract it to the ScummVM directory:
   ```bash
   cd scummvm
   tar -xzf ../scummvm-patches/roomwizard-backend.tar.gz
   cd ..
   ```

2. Sync to version control:
   ```bash
   cd scummvm-roomwizard
   ./manage-scummvm-changes.sh sync
   ```

3. Commit the files to git:
   ```bash
   git add backend-files/
   git commit -m "Migrate backend files to version control"
   ```

## Benefits Over tar.gz

| Aspect | tar.gz | Version Control |
|--------|--------|-----------------|
| File history | ❌ Binary blob | ✅ Per-file history |
| Diffs | ❌ Hard to compare | ✅ Easy to compare |
| Partial changes | ❌ All or nothing | ✅ File-by-file |
| Merge conflicts | ❌ Manual extraction | ✅ Git merge tools |
| Editing | ❌ Extract, edit, repack | ✅ Edit directly |
| Size | ❌ Compressed but opaque | ✅ Efficient git storage |

## Git Integration

These files should be committed to your roomwizard repository:

```bash
git add scummvm-roomwizard/backend-files/
git commit -m "Add ScummVM RoomWizard backend implementation"
```

The ScummVM repository itself remains clean with these files excluded via `.git/info/exclude`.

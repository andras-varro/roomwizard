# Managing ScummVM Changes Without Committing to Upstream

This document explains how your RoomWizard backend changes are kept separate from the ScummVM repository.

## What's Configured

Your ScummVM changes are protected using two git mechanisms:

### 1. Git Local Exclude
The file `scummvm/.git/info/exclude` ignores these untracked files:
- `backends/platform/roomwizard/` - Your custom backend
- `IMPLEMENTATION_SUCCESS.md` - Documentation

Contents of the exclude file:

```ini
# Git local exclude file - these patterns are ignored only in this local repository
# This file is NOT tracked by git, so your exclusions won't affect the upstream repo

# RoomWizard backend implementation
backends/platform/roomwizard/

# RoomWizard documentation
IMPLEMENTATION_SUCCESS.md 
```

### 2. Skip-Worktree
The modified `configure` file is marked with skip-worktree to ignore local changes:
```bash
git update-index --skip-worktree configure
```

## Result
Running `git status` in the scummvm directory shows a clean working tree, even though your changes are present.

## Management Script

Use `manage-scummvm-changes.sh` for common operations:

```bash
# Check current configuration
bash manage-scummvm-changes.sh status

# Create backup of your changes
bash manage-scummvm-changes.sh backup

# Restore from backup
bash manage-scummvm-changes.sh restore

# Update from upstream ScummVM
bash manage-scummvm-changes.sh update
```

## Updating from Upstream

To pull the latest ScummVM updates:

```bash
bash manage-scummvm-changes.sh update
```

This automatically:
1. Temporarily disables skip-worktree
2. Stashes your changes
3. Pulls from upstream
4. Restores your changes
5. Re-enables skip-worktree

## Manual Update (if needed)

```bash
cd scummvm
git update-index --no-skip-worktree configure
git stash
git pull origin branch-2-8
git stash pop
git update-index --skip-worktree configure
```

## Backups

Backups are stored in `scummvm-patches/`:
- `configure.patch` - Your configure modifications
- `roomwizard-backend.tar.gz` - Your custom backend files

Create backups before major changes:
```bash
bash manage-scummvm-changes.sh backup
```

## Important Notes

- Your changes are local only and will never be pushed to ScummVM's repository
- Always create backups before pulling upstream updates
- Test your backend after updating from upstream

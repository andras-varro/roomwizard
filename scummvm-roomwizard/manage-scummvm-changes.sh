#!/bin/bash
# ScummVM RoomWizard Backend Management Script
# This script helps manage your local ScummVM changes without committing to upstream

SCUMMVM_DIR="../scummvm"
BACKEND_FILES_DIR="backend-files"
BACKEND_SRC_DIR="backends/platform/roomwizard"

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo "ScummVM RoomWizard Backend Management"
echo "======================================"
echo ""

# Function to check current status
check_status() {
    echo -e "${GREEN}Checking git status...${NC}"
    cd "$SCUMMVM_DIR"
    git status
    echo ""
    
    echo -e "${GREEN}Files marked with skip-worktree:${NC}"
    git ls-files -v | grep '^S'
    echo ""
    
    echo -e "${GREEN}Local exclude patterns:${NC}"
    cat .git/info/exclude
    cd - > /dev/null
    
    echo ""
    echo -e "${GREEN}Backend files status:${NC}"
    if [ -d "$BACKEND_FILES_DIR" ]; then
        echo "Version-controlled backend files in $BACKEND_FILES_DIR:"
        ls -lh "$BACKEND_FILES_DIR"
    else
        echo -e "${RED}Backend files directory not found!${NC}"
    fi
}

# Function to sync backend files to version control
sync_to_vcs() {
    echo -e "${GREEN}Syncing backend files to version control...${NC}"
    
    # Create backend-files directory if it doesn't exist
    mkdir -p "$BACKEND_FILES_DIR"
    
    # Save configure changes (temporarily disable skip-worktree)
    cd "$SCUMMVM_DIR"
    git update-index --no-skip-worktree configure
    git diff configure > "../$BACKEND_FILES_DIR/configure.patch"
    git update-index --skip-worktree configure
    cd ..
    
    # Copy backend source files
    if [ -d "$SCUMMVM_DIR/$BACKEND_SRC_DIR" ]; then
        echo "Copying backend source files..."
        cp "$SCUMMVM_DIR/$BACKEND_SRC_DIR"/*.cpp "$BACKEND_FILES_DIR/" 2>/dev/null
        cp "$SCUMMVM_DIR/$BACKEND_SRC_DIR"/*.h "$BACKEND_FILES_DIR/" 2>/dev/null
        cp "$SCUMMVM_DIR/$BACKEND_SRC_DIR"/*.mk "$BACKEND_FILES_DIR/" 2>/dev/null
        cp "$SCUMMVM_DIR/$BACKEND_SRC_DIR"/README.md "$BACKEND_FILES_DIR/" 2>/dev/null
        echo -e "${GREEN}✓ Backend files synced to $BACKEND_FILES_DIR${NC}"
    else
        echo -e "${RED}Backend directory not found in ScummVM!${NC}"
        cd ..
        return 1
    fi
    
    # Copy IMPLEMENTATION_SUCCESS.md if it exists
    if [ -f "$SCUMMVM_DIR/IMPLEMENTATION_SUCCESS.md" ]; then
        cp "$SCUMMVM_DIR/IMPLEMENTATION_SUCCESS.md" "$BACKEND_FILES_DIR/"
        echo -e "${GREEN}✓ Copied IMPLEMENTATION_SUCCESS.md${NC}"
    fi
    
    echo ""
    echo -e "${GREEN}Files now in version control:${NC}"
    ls -lh "$BACKEND_FILES_DIR"
}

# Function to restore backend files from version control
restore_from_vcs() {
    echo -e "${YELLOW}Restoring backend files from version control...${NC}"
    
    if [ ! -d "$BACKEND_FILES_DIR" ]; then
        echo -e "${RED}Backend files directory not found!${NC}"
        echo "Run 'sync' command first to create version-controlled files."
        return 1
    fi
    
    cd "$SCUMMVM_DIR"
    
    # Apply configure patch
    if [ -f "../$BACKEND_FILES_DIR/configure.patch" ]; then
        git apply "../$BACKEND_FILES_DIR/configure.patch"
        echo -e "${GREEN}✓ Applied configure.patch${NC}"
    else
        echo -e "${YELLOW}⚠ configure.patch not found${NC}"
    fi
    
    # Create backend directory if it doesn't exist
    mkdir -p "$BACKEND_SRC_DIR"
    
    # Copy backend source files
    echo "Restoring backend source files..."
    cp "../$BACKEND_FILES_DIR"/*.cpp "$BACKEND_SRC_DIR/" 2>/dev/null
    cp "../$BACKEND_FILES_DIR"/*.h "$BACKEND_SRC_DIR/" 2>/dev/null
    cp "../$BACKEND_FILES_DIR"/*.mk "$BACKEND_SRC_DIR/" 2>/dev/null
    cp "../$BACKEND_FILES_DIR"/README.md "$BACKEND_SRC_DIR/" 2>/dev/null
    echo -e "${GREEN}✓ Restored backend files to $BACKEND_SRC_DIR${NC}"
    
    # Restore IMPLEMENTATION_SUCCESS.md if it exists
    if [ -f "../$BACKEND_FILES_DIR/IMPLEMENTATION_SUCCESS.md" ]; then
        cp "../$BACKEND_FILES_DIR/IMPLEMENTATION_SUCCESS.md" .
        echo -e "${GREEN}✓ Restored IMPLEMENTATION_SUCCESS.md${NC}"
    fi
    
    cd ..
}

# Function to create backup patch (legacy support)
create_backup() {
    echo -e "${YELLOW}Note: This creates a legacy tar.gz backup.${NC}"
    echo -e "${YELLOW}Consider using 'sync' command for version-controlled files.${NC}"
    echo ""
    echo -e "${GREEN}Creating backup patch...${NC}"
    cd "$SCUMMVM_DIR"
    
    # Create patches directory if it doesn't exist
    mkdir -p ../scummvm-patches
    
    # Save configure changes (temporarily disable skip-worktree)
    git update-index --no-skip-worktree configure
    git diff configure > ../scummvm-patches/configure.patch
    git update-index --skip-worktree configure
    
    # Archive custom files
    tar -czf ../scummvm-patches/roomwizard-backend.tar.gz \
        backends/platform/roomwizard/ \
        IMPLEMENTATION_SUCCESS.md 2>/dev/null
    
    echo -e "${GREEN}✓ Backup created in ../scummvm-patches/${NC}"
    echo "  - configure.patch"
    echo "  - roomwizard-backend.tar.gz"
    cd ..
}

# Function to restore from backup (legacy support)
restore_backup() {
    echo -e "${YELLOW}Note: This restores from legacy tar.gz backup.${NC}"
    echo -e "${YELLOW}Consider using 'restore' command for version-controlled files.${NC}"
    echo ""
    echo -e "${YELLOW}Restoring from backup...${NC}"
    cd "$SCUMMVM_DIR"
    
    if [ -f ../scummvm-patches/configure.patch ]; then
        git apply ../scummvm-patches/configure.patch
        echo -e "${GREEN}✓ Applied configure.patch${NC}"
    fi
    
    if [ -f ../scummvm-patches/roomwizard-backend.tar.gz ]; then
        tar -xzf ../scummvm-patches/roomwizard-backend.tar.gz
        echo -e "${GREEN}✓ Extracted roomwizard-backend.tar.gz${NC}"
    fi
    
    cd ..
}

# Function to update from upstream
update_upstream() {
    echo -e "${YELLOW}Updating from upstream...${NC}"
    
    # First, sync current state to version control
    echo "Syncing current state before update..."
    sync_to_vcs
    
    cd "$SCUMMVM_DIR"
    
    # Temporarily disable skip-worktree
    echo "Temporarily disabling skip-worktree..."
    git update-index --no-skip-worktree configure
    
    # Stash local changes
    echo "Stashing local changes..."
    git stash push -m "RoomWizard changes - $(date +%Y%m%d-%H%M%S)"
    
    # Pull from upstream
    echo "Pulling from upstream..."
    git pull origin branch-2-8
    
    # Restore changes
    echo "Restoring local changes..."
    git stash pop
    
    # Re-enable skip-worktree
    echo "Re-enabling skip-worktree..."
    git update-index --skip-worktree configure
    
    cd ..
    
    # Sync updated state
    echo "Syncing updated state..."
    sync_to_vcs
    
    echo -e "${GREEN}✓ Update complete${NC}"
}

# Function to setup git ignore configuration
setup_ignore() {
    echo -e "${GREEN}Setting up git ignore configuration...${NC}"
    cd "$SCUMMVM_DIR"
    
    # Add to local exclude
    cat >> .git/info/exclude << 'EOF'

# RoomWizard backend implementation
backends/platform/roomwizard/

# RoomWizard documentation
IMPLEMENTATION_SUCCESS.md
EOF
    
    # Mark configure with skip-worktree
    git update-index --skip-worktree configure
    
    echo -e "${GREEN}✓ Git ignore configuration complete${NC}"
    cd ..
}

# Function to reset skip-worktree
reset_skipworktree() {
    echo -e "${YELLOW}Resetting skip-worktree for configure...${NC}"
    cd "$SCUMMVM_DIR"
    git update-index --no-skip-worktree configure
    echo -e "${GREEN}✓ Skip-worktree disabled for configure${NC}"
    cd ..
}

# Main menu
case "${1:-}" in
    status)
        check_status
        ;;
    sync)
        sync_to_vcs
        ;;
    restore)
        restore_from_vcs
        ;;
    backup)
        create_backup
        ;;
    backup-restore)
        restore_backup
        ;;
    update)
        update_upstream
        ;;
    setup)
        setup_ignore
        ;;
    reset)
        reset_skipworktree
        ;;
    *)
        echo "Usage: $0 {status|sync|restore|backup|backup-restore|update|setup|reset}"
        echo ""
        echo "Commands:"
        echo "  status         - Check current git status and configuration"
        echo "  sync           - Sync backend files TO version control (recommended)"
        echo "  restore        - Restore backend files FROM version control (recommended)"
        echo "  backup         - Create legacy tar.gz backup (old method)"
        echo "  backup-restore - Restore from legacy tar.gz backup (old method)"
        echo "  update         - Update from upstream (pulls and syncs changes)"
        echo "  setup          - Setup git ignore configuration (run once)"
        echo "  reset          - Reset skip-worktree for configure file"
        echo ""
        echo "Recommended workflow:"
        echo "  $0 sync     # Sync your changes to version control"
        echo "  $0 update   # Pull latest ScummVM updates"
        echo "  $0 restore  # Restore your backend files after update"
        echo ""
        echo "Legacy workflow (tar.gz):"
        echo "  $0 backup          # Create tar.gz backup"
        echo "  $0 backup-restore  # Restore from tar.gz"
        exit 1
        ;;
esac

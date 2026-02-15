#!/bin/bash
# ScummVM RoomWizard Backend Management Script
# This script helps manage your local ScummVM changes without committing to upstream

SCUMMVM_DIR="scummvm"

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
    cd ..
}

# Function to create backup patch
create_backup() {
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

# Function to restore from backup
restore_backup() {
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
    
    echo -e "${GREEN}✓ Update complete${NC}"
    cd ..
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
    backup)
        create_backup
        ;;
    restore)
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
        echo "Usage: $0 {status|backup|restore|update|setup|reset}"
        echo ""
        echo "Commands:"
        echo "  status  - Check current git status and configuration"
        echo "  backup  - Create backup patches of your changes"
        echo "  restore - Restore from backup patches"
        echo "  update  - Update from upstream (pulls and restores changes)"
        echo "  setup   - Setup git ignore configuration (run once)"
        echo "  reset   - Reset skip-worktree for configure file"
        echo ""
        echo "Examples:"
        echo "  $0 status   # Check what's configured"
        echo "  $0 backup   # Create a backup before major changes"
        echo "  $0 update   # Pull latest ScummVM updates"
        exit 1
        ;;
esac

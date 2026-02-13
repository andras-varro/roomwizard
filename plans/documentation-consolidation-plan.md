# RoomWizard Documentation Consolidation Plan

## Overview

This plan consolidates the RoomWizard documentation into logical, focused documents while preserving [`PROJECT_STATUS.md`](../PROJECT_STATUS.md) as the active development tracker.

## Current Documentation Structure

### Root Level (15 files)
1. **analysis.md** (561 lines) - Comprehensive firmware analysis
2. **analysis_summary.md** (359 lines) - Executive summary of analysis
3. **usb-analyzis.md** (204 lines) - USB port functionality analysis
4. **BRICK_BREAKER_README.md** (302 lines) - Browser game implementation
5. **DEPLOYMENT_GUIDE.md** (345 lines) - Browser game deployment
6. **LED_CONTROL_IMPLEMENTATION.md** (244 lines) - LED control technical details
7. **LED_CONTROL_QUICK_START.md** (216 lines) - LED control quick reference
8. **MODIFICATION_STATUS.md** (217 lines) - Browser modification status
9. **GAME_MODE_SETUP.md** (468 lines) - Native games setup guide
10. **PERMANENT_GAME_MODE.md** (338 lines) - Permanent game mode installation
11. **TOUCH_INPUT_FIX.md** (214 lines) - Touch input coordinate fix
12. **SSH_ROOT_ACCESS_SETUP.md** (current) - SSH access guide
13. **steps-to-strip-down.md** (643 lines) - System stripping guide
14. **PROJECT_STATUS.md** (427 lines) - **KEEP AS-IS** - Active development tracker
15. **bouncing-ball.html** (3004 chars) - Demo HTML file

### Native Games Directory (5 files)
1. **README.md** (395 lines) - Native games overview
2. **PROJECT_SUMMARY.md** (227 lines) - Project summary
3. **QUICKSTART.md** (183 lines) - Quick start guide
4. **HARDWARE_CONTROL.md** (436 lines) - Hardware control API
5. **HARDWARE_QUICK_REF.md** (142 lines) - Hardware quick reference

## Proposed Consolidated Structure

```
roomwizard/
├── README.md                          # NEW: Master navigation document
├── PROJECT_STATUS.md                  # KEEP: Active development tracker
│
├── SYSTEM_ANALYSIS.md                 # NEW: Consolidated system analysis
├── NATIVE_GAMES_GUIDE.md             # NEW: Consolidated native games guide
├── BROWSER_MODIFICATIONS.md          # NEW: Consolidated browser modifications
├── SYSTEM_SETUP.md                   # NEW: Consolidated system setup
│
├── backup_docs/                       # NEW: Original files backup
│   ├── analysis.md
│   ├── analysis_summary.md
│   ├── usb-analyzis.md
│   ├── BRICK_BREAKER_README.md
│   ├── DEPLOYMENT_GUIDE.md
│   ├── LED_CONTROL_IMPLEMENTATION.md
│   ├── LED_CONTROL_QUICK_START.md
│   ├── MODIFICATION_STATUS.md
│   ├── GAME_MODE_SETUP.md
│   ├── PERMANENT_GAME_MODE.md
│   ├── TOUCH_INPUT_FIX.md
│   ├── SSH_ROOT_ACCESS_SETUP.md
│   └── steps-to-strip-down.md
│
├── native_games/
│   ├── README.md                      # KEEP: Main native games docs
│   └── backup_docs/                   # NEW: Backup of consolidated docs
│       ├── PROJECT_SUMMARY.md
│       ├── QUICKSTART.md
│       ├── HARDWARE_CONTROL.md
│       └── HARDWARE_QUICK_REF.md
│
└── plans/                             # KEEP: Planning documents
    └── (existing plan files)
```

## Consolidation Details

### 1. SYSTEM_ANALYSIS.md
**Purpose:** Complete technical analysis of RoomWizard hardware and firmware

**Combines:**
- analysis.md (comprehensive analysis)
- analysis_summary.md (executive summary)
- usb-analyzis.md (USB port analysis)

**Structure:**
```markdown
# RoomWizard System Analysis

## Executive Summary
[From analysis_summary.md]

## System Architecture
[From analysis.md - architecture sections]

## Hardware Components
[From analysis.md - hardware sections]

## USB Subsystem
[From usb-analyzis.md]

## Protection Mechanisms
[From analysis.md - protection sections]

## Hardware Control Interfaces
[From analysis.md - LED/touch/display sections]

## Application Framework
[From analysis.md - software stack]

## Safe Modification Strategy
[From analysis.md - modification sections]
```

### 2. NATIVE_GAMES_GUIDE.md
**Purpose:** Complete guide for native C games development and deployment

**Combines:**
- GAME_MODE_SETUP.md (setup guide)
- PERMANENT_GAME_MODE.md (permanent installation)
- TOUCH_INPUT_FIX.md (touch input fix)

**Structure:**
```markdown
# RoomWizard Native Games Guide

## Overview
[Introduction to native games approach]

## Quick Start
[Fast-track setup]

## Game Mode Setup
[From GAME_MODE_SETUP.md]

## Permanent Game Mode Installation
[From PERMANENT_GAME_MODE.md]

## Touch Input System
[From TOUCH_INPUT_FIX.md]

## Troubleshooting
[Combined troubleshooting sections]

## Development
[Adding new games, customization]
```

### 3. BROWSER_MODIFICATIONS.md
**Purpose:** Complete guide for browser-based game modifications

**Combines:**
- BRICK_BREAKER_README.md (game implementation)
- DEPLOYMENT_GUIDE.md (deployment process)
- LED_CONTROL_IMPLEMENTATION.md (LED control backend)
- LED_CONTROL_QUICK_START.md (LED quick reference)
- MODIFICATION_STATUS.md (modification status)

**Structure:**
```markdown
# RoomWizard Browser Modifications Guide

## Overview
[Introduction to browser modifications]

## Brick Breaker Game
[From BRICK_BREAKER_README.md]

## LED Control System
[From LED_CONTROL_IMPLEMENTATION.md + LED_CONTROL_QUICK_START.md]

## Deployment Process
[From DEPLOYMENT_GUIDE.md]

## Modification Status
[From MODIFICATION_STATUS.md]

## Troubleshooting
[Combined troubleshooting sections]
```

### 4. SYSTEM_SETUP.md
**Purpose:** System configuration and access setup

**Combines:**
- SSH_ROOT_ACCESS_SETUP.md (SSH access)
- steps-to-strip-down.md (system stripping)

**Structure:**
```markdown
# RoomWizard System Setup Guide

## Overview
[Introduction to system setup]

## SSH Root Access Setup
[From SSH_ROOT_ACCESS_SETUP.md]

## Minimal CLI System Setup
[From steps-to-strip-down.md]

## Network Configuration
[Combined network sections]

## Security Considerations
[Combined security sections]
```

### 5. README.md (Master Navigation)
**Purpose:** Entry point with navigation to all documentation

**Structure:**
```markdown
# RoomWizard Project Documentation

## Quick Navigation

### Active Development
- [Project Status](PROJECT_STATUS.md) - Current development status and tasks

### System Documentation
- [System Analysis](SYSTEM_ANALYSIS.md) - Hardware and firmware analysis
- [System Setup](SYSTEM_SETUP.md) - SSH access and system configuration

### Game Development
- [Native Games Guide](NATIVE_GAMES_GUIDE.md) - C games development
- [Native Games README](native_games/README.md) - Native games overview

### Browser Modifications
- [Browser Modifications](BROWSER_MODIFICATIONS.md) - Browser-based games

### Planning Documents
- [Plans Directory](plans/) - Technical plans and workflows

## Project Overview
[Brief description of the RoomWizard project]

## Getting Started
[Quick links to most common tasks]
```

### 6. Native Games Directory Consolidation

The native_games directory will be simplified:
- **KEEP:** README.md (comprehensive, well-structured)
- **BACKUP:** PROJECT_SUMMARY.md, QUICKSTART.md, HARDWARE_CONTROL.md, HARDWARE_QUICK_REF.md

The README.md already contains most of the information from the other files and is the most complete reference.

## Implementation Steps

### Phase 1: Create Backup Structure
1. Create `backup_docs/` directory in root
2. Create `backup_docs/` directory in native_games/
3. Copy all files to be consolidated into backup directories

### Phase 2: Create Consolidated Documents
1. Create SYSTEM_ANALYSIS.md
2. Create NATIVE_GAMES_GUIDE.md
3. Create BROWSER_MODIFICATIONS.md
4. Create SYSTEM_SETUP.md
5. Create master README.md

### Phase 3: Update References
1. Update PROJECT_STATUS.md documentation references
2. Update any cross-references in consolidated documents
3. Ensure all internal links work correctly

### Phase 4: Move Original Files
1. Move original files to backup_docs/
2. Verify no broken links
3. Test navigation flow

## Benefits of This Structure

### Clarity
- **4 focused documents** instead of 13 scattered files
- Clear separation of concerns
- Logical grouping by use case

### Maintainability
- Easier to update related information
- Reduced duplication
- Single source of truth for each topic

### Discoverability
- Master README provides clear navigation
- Related information is co-located
- Logical document names

### Preservation
- All original content preserved in backup_docs/
- PROJECT_STATUS.md remains active development tracker
- No information loss

## Document Size Estimates

| Document | Estimated Lines | Content Sources |
|----------|----------------|-----------------|
| SYSTEM_ANALYSIS.md | ~800 | 3 files combined |
| NATIVE_GAMES_GUIDE.md | ~700 | 3 files combined |
| BROWSER_MODIFICATIONS.md | ~900 | 5 files combined |
| SYSTEM_SETUP.md | ~700 | 2 files combined |
| README.md | ~150 | New navigation doc |

## Success Criteria

- [ ] All original content preserved in backup_docs/
- [ ] 4 consolidated documents created with logical structure
- [ ] Master README.md provides clear navigation
- [ ] PROJECT_STATUS.md updated with new references
- [ ] All internal links functional
- [ ] No duplicate information across documents
- [ ] Clear separation of concerns maintained

## Notes

- The native_games/README.md is already well-structured and comprehensive, so minimal consolidation needed there
- PROJECT_STATUS.md is actively tracking development, so it should remain as-is
- The plans/ directory contains historical planning documents and should be preserved
- bouncing-ball.html is a demo file and can remain in root or be moved to a demos/ directory

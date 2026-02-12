# RoomWizard Modification Workflow

## High-Level Process Flow

```mermaid
graph TB
    A[Start: Extracted Partitions] --> B[Create Bouncing Ball HTML]
    B --> C[Mount Root Filesystem Image]
    C --> D[Backup Original index.html]
    D --> E[Replace with Bouncing Ball]
    E --> F[Set File Permissions]
    F --> G[Unmount Filesystem]
    G --> H[Regenerate MD5 Checksum]
    H --> I[Verify All MD5 Files]
    I --> J[Run Filesystem Check]
    J --> K{All Checks Pass?}
    K -->|Yes| L[Ready for Deployment]
    K -->|No| M[Fix Issues]
    M --> C
    L --> N[Write to SD Card]
    N --> O[Boot RoomWizard]
    O --> P{Boot Successful?}
    P -->|Yes| Q[Success: Bouncing Ball Running]
    P -->|No| R[Recovery: Restore Backup]
```

## Detailed Boot Sequence After Modification

```mermaid
graph TB
    A[Power On] --> B[U-Boot Bootloader]
    B --> C[Load uImage-system Kernel]
    C --> D[Mount Root Filesystem]
    D --> E{MD5 Check}
    E -->|Pass| F[Init System Starts]
    E -->|Fail| G[Boot Failure]
    F --> H[S30x11: Start Xorg]
    H --> I[S35browser: Start Browser]
    I --> J[Jetty Serves index.html]
    J --> K[Browser Loads Bouncing Ball]
    K --> L[Watchdog Fed by Browser]
    L --> M[System Stable]
    G --> N[Increment Boot Tracker]
    N --> O{Tracker >= 2?}
    O -->|Yes| P[Execute fail.sh Recovery]
    O -->|No| A
```

## File Modification Impact

```mermaid
graph LR
    A[Original System] --> B[Modified System]
    
    subgraph Original
    A1[index.html: RoomWizard UI] --> A2[Jetty Serves]
    A2 --> A3[Browser Displays]
    A3 --> A4[Room Booking App]
    end
    
    subgraph Modified
    B1[index.html: Bouncing Ball] --> B2[Jetty Serves]
    B2 --> B3[Browser Displays]
    B3 --> B4[Bouncing Ball Animation]
    end
    
    style B1 fill:#ff9
    style B4 fill:#9f9
```

## Protection Mechanisms Status

```mermaid
graph TB
    subgraph Protection Layers
    A[MD5 Verification] --> A1{Status}
    A1 -->|Modified| A2[Regenerated ✓]
    
    B[Watchdog Timer] --> B1{Status}
    B1 -->|Active| B2[Browser Feeds ✓]
    
    C[Control Block] --> C1{Status}
    C1 -->|Unchanged| C2[Tracker = 0 ✓]
    
    D[Boot Chain] --> D1{Status}
    D1 -->|Unchanged| D2[Normal Boot ✓]
    end
    
    style A2 fill:#9f9
    style B2 fill:#9f9
    style C2 fill:#9f9
    style D2 fill:#9f9
```

## Risk Mitigation Strategy

```mermaid
graph TB
    A[Potential Risks] --> B[MD5 Mismatch]
    A --> C[Watchdog Timeout]
    A --> D[Boot Failure]
    A --> E[File Corruption]
    
    B --> B1[Mitigation: Regenerate MD5]
    C --> C1[Mitigation: Browser Process Active]
    D --> D1[Mitigation: Auto-Recovery After 2 Boots]
    E --> E1[Mitigation: Filesystem Check + Backup]
    
    B1 --> F[Low Risk]
    C1 --> F
    D1 --> F
    E1 --> F
    
    style F fill:#9f9
```

## Deployment Decision Tree

```mermaid
graph TB
    A[Ready to Deploy?] --> B{Backup Exists?}
    B -->|No| C[Create Backup First]
    B -->|Yes| D{MD5 Valid?}
    C --> D
    D -->|No| E[Regenerate MD5]
    D -->|Yes| F{Filesystem OK?}
    E --> F
    F -->|No| G[Run fsck Repair]
    F -->|Yes| H{Serial Console Ready?}
    G --> H
    H -->|No| I[Connect Serial Console]
    H -->|Yes| J[Deploy to SD Card]
    I --> J
    J --> K[Monitor Boot Logs]
    K --> L{Boot Success?}
    L -->|Yes| M[Verify Bouncing Ball]
    L -->|No| N[Check Logs]
    M --> O[Success!]
    N --> P{Recoverable?}
    P -->|Yes| Q[Apply Fix]
    P -->|No| R[Restore Backup]
    Q --> J
    
    style O fill:#9f9
    style R fill:#f99
```

## Component Interaction Map

```mermaid
graph TB
    subgraph Hardware
    H1[Display/Touchscreen]
    H2[LED Indicators]
    H3[Watchdog Timer]
    end
    
    subgraph Software Stack
    S1[Linux Kernel]
    S2[X11 Display Server]
    S3[Jetty Web Server]
    S4[WebKit Browser]
    S5[Bouncing Ball HTML/JS]
    end
    
    S1 --> S2
    S1 --> S3
    S2 --> S4
    S3 --> S5
    S4 --> S5
    
    S5 --> H1
    S4 --> H3
    S1 --> H2
    
    H1 -.Touch Events.-> S5
    H3 -.Heartbeat.-> S4
    
    style S5 fill:#ff9
    style H1 fill:#9cf
```

## File System Layout

```
Root Filesystem (sd_rootfs_part.img)
│
├── /opt/
│   ├── jetty-9-4-11/
│   │   ├── webapps/
│   │   │   └── frontpanel/
│   │   │       └── pages/
│   │   │           ├── index.html ← MODIFIED
│   │   │           └── index.html.original ← BACKUP
│   │   └── etc/
│   │       └── configure_jetty.sh
│   │
│   ├── openjre-8/
│   │   └── bin/java
│   │
│   └── sbin/
│       └── (system scripts)
│
├── /etc/
│   ├── init.d/
│   │   ├── x11 ← Starts display
│   │   ├── webserver ← Starts Jetty
│   │   ├── browser ← Starts WebKit
│   │   └── watchdog ← Monitors system
│   │
│   └── scriptlookup.txt
│
└── /var/log/
    ├── browser.out
    ├── browser.err
    └── jettystart
```

## Success Criteria Checklist

```mermaid
graph TB
    A[Deployment Complete] --> B{System Boots?}
    B -->|Yes| C{X11 Starts?}
    B -->|No| FAIL1[Check Boot Logs]
    
    C -->|Yes| D{Jetty Starts?}
    C -->|No| FAIL2[Check X11 Logs]
    
    D -->|Yes| E{Browser Starts?}
    D -->|No| FAIL3[Check Jetty Logs]
    
    E -->|Yes| F{Ball Displays?}
    E -->|No| FAIL4[Check Browser Logs]
    
    F -->|Yes| G{Ball Animates?}
    F -->|No| FAIL5[Check HTML/JS]
    
    G -->|Yes| H{Touch Works?}
    G -->|No| FAIL6[Check Canvas Rendering]
    
    H -->|Yes| I{System Stable?}
    H -->|No| FAIL7[Check Touch Events]
    
    I -->|Yes| SUCCESS[Complete Success!]
    I -->|No| FAIL8[Check Watchdog]
    
    style SUCCESS fill:#9f9
    style FAIL1 fill:#f99
    style FAIL2 fill:#f99
    style FAIL3 fill:#f99
    style FAIL4 fill:#f99
    style FAIL5 fill:#f99
    style FAIL6 fill:#f99
    style FAIL7 fill:#f99
    style FAIL8 fill:#f99
```

## Recovery Workflow

```mermaid
graph TB
    A[Boot Failure Detected] --> B{Boot Tracker Value?}
    B -->|0| C[Increment to 1, Retry Boot]
    B -->|1| D[Increment to 2, Retry Boot]
    B -->|2+| E[Execute fail.sh Recovery]
    
    C --> F[Reboot]
    D --> F
    
    E --> G[Load Backup Partition]
    G --> H[Restore Previous Firmware]
    H --> I[Reset Tracker to 0]
    I --> F
    
    F --> J{Boot Success?}
    J -->|Yes| K[System Recovered]
    J -->|No| L[Manual Intervention Required]
    
    L --> M[Remove SD Card]
    M --> N[Restore from Backup]
    N --> O[Reinsert SD Card]
    O --> P[System Restored]
    
    style K fill:#9f9
    style P fill:#9f9
```

## Timeline of Events

```mermaid
gantt
    title RoomWizard Boot Sequence with Modified Firmware
    dateFormat ss
    axisFormat %S sec
    
    section Boot
    U-Boot Loads Kernel           :00, 5s
    Kernel Initializes            :05, 10s
    
    section Init
    Mount Filesystems             :15, 5s
    MD5 Verification              :20, 3s
    Start Init Scripts            :23, 2s
    
    section Services
    Start X11 Display             :25, 5s
    Start Jetty Server            :30, 10s
    Start Browser                 :40, 5s
    
    section Application
    Load Bouncing Ball HTML       :45, 2s
    Initialize Canvas             :47, 1s
    Start Animation               :48, 1s
    System Ready                  :49, 1s
    
    section Watchdog
    Watchdog Active               :25, 25s
```

## Key Takeaways

### What Changes
- **Single File**: `/opt/jetty-9-4-11/webapps/frontpanel/pages/index.html`
- **One Checksum**: `sd_rootfs_part.img.md5`

### What Stays the Same
- Boot sequence and bootloader
- All init scripts and services
- X11, Jetty, Browser processes
- Watchdog configuration
- Hardware interfaces

### Why This Works
- Minimal intervention reduces risk
- Leverages existing infrastructure
- Browser process maintains watchdog
- MD5 regeneration satisfies integrity checks
- No changes to critical boot components

### Safety Net
- Original SD card backed up
- Auto-recovery after 2 failed boots
- Serial console for debugging
- Simple rollback procedure
- index.html.original preserved
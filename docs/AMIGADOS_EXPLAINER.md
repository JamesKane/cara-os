# AmigaDOS: The Architect's Soul

To understand CaraOS, you must understand the architecture it resurrects. AmigaOS wasn't just another OS from the 80s; it was a radical departure from the "mainframe-lite" philosophy of its contemporaries.

## 📜 A Brief History: From TRIPOS to V36

The story of AmigaDOS is a tale of two lineages merging:

1.  **The Silicon (The Lorraine):** In the early 80s, a team at Amiga Corp led by **Jay Miner** (the "Father of the Amiga") was designing a revolutionary 16-bit game console. They built custom chips (Agnus, Denise, Paula) that could move data and draw graphics without bothering the CPU.
2.  **The Software (TRIPOS):** As the project evolved into a computer, they needed an OS. **Carl Sassenrath** designed the multitasking executive (**Exec**), but they lacked a disk operating system. They licensed **TRIPOS**, a portable OS written in BCPL at Cambridge University.

When the Amiga 1000 launched in 1985, it was a hybrid: Sassenrath's elegant, message-passing Exec kernel hosting a BCPL-based DOS. 

**Release 2 (V36+):** This is the version CaraOS targets. Released in 1990, it was the "Great Modernization." The OS was largely rewritten in C, the UI was overhauled (Workbench 2.0), and the architecture was stabilized into the legendary 3rd Edition Reference Kernel Manuals (RKMs). It is the pinnacle of the Amiga's architectural design.

## 🏛️ Key Architects

-   **Jay Miner:** The hardware visionary. He believed the OS should stay out of the way of the custom silicon. His focus was on high-bandwidth throughput.
-   **Carl Sassenrath:** The software genius behind **Exec**. He implemented a prioritized, preemptive multitasking kernel that used pointer-passing IPC. It was years ahead of its time.
-   **RJ Mical:** The architect of **Intuition**. He built the windowing system and the original user interface, creating the "Screens and Windows" metaphor that allowed multiple applications to share the hardware gracefully.
-   **Tim King:** Led the team that ported TRIPOS to the Amiga, giving birth to the original AmigaDOS.

## 🖼️ Intuition: The Visual Matrix

While Exec managed the heartbeat and AmigaDOS managed the memory of the machine, **Intuition** was its face. 

Unlike the monolithic GUIs of the era, Intuition was deeply integrated with Exec's message-passing. Every mouse click or keypress was delivered as an `IntuiMessage` to a task's message port. It pioneered the concept of multiple **Screens**—virtual desktops with different resolutions and color depths that you could drag down to reveal the screen behind them.

In **Release 2 (V36+)**, Intuition evolved with **BOOPSI** (Basic Object-Oriented Programming System for Intuition), allowing for a highly modular, extensible gadget and window system. CaraOS resurrects this as **Leargas**, keeping the elegant "Screens and Windows" model but stripping away the legacy display constraints.

## ⚡ Why This Model for Modern Hobbyists?

In a world of multi-gigabyte OS kernels and "Security-as-a-Service" hardware, the Amiga model is a breath of fresh air for three reasons:

### 1. The "User is God" Philosophy
Modern OSes (Linux, macOS, Windows) are designed to protect the system from you. They use complex privilege rings and MMU walls that add latency and cognitive load. In the Amiga model, the MMU is a tool for *isolation* (crash protection), not *restriction*. You own the silicon.

### 2. Pointer-Passing IPC (The Speed)
In UNIX, sending data between programs involves "copying": you copy data from user-space to kernel-space, then back to another user-space. It's safe, but slow. 
AmigaOS uses **Message Ports**. To send a 4K image, you don't copy it; you just send a 64-bit pointer to the memory address. It is instantaneous. On modern RISC-V hardware, this makes the system feel "electric."

### 3. Radical Simplicity
The entire AmigaOS specification is contained in a few volumes of RKMs. A single person can understand the *entire* stack, from the bootloader to the window manager. This "total comprehension" is the ultimate goal for any retro-compute fan or OS hobbyist.

## 🦾 CaraOS: The Resurrection
CaraOS isn't an emulator; it's a cleanroom implementation of these ideas using **C23** and **RISC-V**. We keep the pointer-passing speed and the architectural elegance of V36+, but we leave the 1980s hardware limitations behind. 

**Welcome to the Silicon Rebellion.**

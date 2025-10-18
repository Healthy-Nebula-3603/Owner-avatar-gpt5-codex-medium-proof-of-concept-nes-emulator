<img width="790" height="772" alt="VirtualBox_ubuntu_22_04_SERVER_kiosk_DIRTY_18_10_2025_18_43_09" src="https://github.com/user-attachments/assets/11b84db0-ec31-4cfa-8b0e-a8cba8efa0fc" />
<img width="781" height="774" alt="VirtualBox_ubuntu_22_04_SERVER_kiosk_DIRTY_18_10_2025_18_42_23" src="https://github.com/user-attachments/assets/8654f897-2867-4eea-9d16-d8866c394200" />
<img width="791" height="777" alt="VirtualBox_ubuntu_22_04_SERVER_kiosk_DIRTY_18_10_2025_18_42_02" src="https://github.com/user-attachments/assets/be961cdf-b147-4e5a-8cac-da7a191c7fbf" />
<img width="787" height="781" alt="VirtualBox_ubuntu_22_04_SERVER_kiosk_DIRTY_18_10_2025_18_41_27" src="https://github.com/user-attachments/assets/6dc5a632-f573-4db8-adda-77fe64c35453" />




Currently using codex-cli with GPT 5 codex medium - one shot - 25 min
I have a plus account to build this codex used 25% of my 5 hours limit. 

The first attempt was using codex-cli with GPT-5 thinking high - one shot -  45 min 
Results: Emulator was only showing title screen of some nes games but no playable 
link
https://www.reddit.com/r/singularity/comments/1nfibtq/within_40_min_codexcli_with_gpt5_high_made_fully/?utm_source=share&utm_medium=web3x&utm_name=web3xcss&utm_term=1&utm_content=share_button

NOW the results are far better - games are playable but with some graphic glitches. I I suspect the second prompt describing what is wrong would fix it. 

Firstly I asked GPT5 chat for a prompt to build fully working a NES emulator in clean C.
I got a PROMPT:



"
You are to implement a **Nintendo NES emulator** in **clean, portable C (C11)** that runs on Linux/macOS/Windows and loads standard **.nes (iNES 1.0) ROM images**. The project must build with a single `Makefile` using `cc` and depend only on **SDL2** for video, audio, and input. Favor clarity, strict correctness, and good structure over cleverness.

## GOAL & SCOPE
- Implement a playable NES emulator with:
  1) CPU: Ricoh 2A03 (MOS 6502 derivative, **no BCD**). Cycle-accurate instruction timing.
  2) PPU: NES PPU with scanline-accurate rendering (not necessarily pixel-perfect), supporting:
     - Background + sprites
     - Name/attribute tables, palettes, scrolling (fine/coarse), sprite evaluation, sprite 0 hit, sprite overflow flag per NES behavior
     - Vertical/horizontal mirroring per cartridge
  3) APU: Basic, accurate-enough audio (pulse 1/2, triangle, noise, DMC). Use SDL2 audio callback.
  4) Controllers: Standard 2 gamepads (strobe/shift register protocol).
  5) Mappers: At minimum **NROM (0/180)**, **MMC1 (1)**, **UxROM (2)**, **CNROM (3)**, and **MMC3 (4)** good enough to run many classics.
  6) File format: **iNES 1.0** header parse; refuse NES 2.0 unless clearly supported.
  7) Timing: NTSC (60.098 Hz) as primary; PAL optional. Maintain CPU:PPU:APU relationship (PPU ~3× CPU).
  8) Save states (optional but nice): single file dump of CPU/PPU/APU/cartridge/mapper.
  9) Battery-backed PRG-RAM persistence for mappers that support it (write a `.sav` next to the ROM).

## NON-GOALS
- FDS, exotic mappers, run-ahead, netplay, shaders, rewind. Keep it classic and clean first.
"
-------------------------------------------------------------------------------------------------

# ADR-007: the dope card with windage, its place on the level view, and the Kestrel link

Status: **Open** 2026-11-07. Approved by the owner ("Approve, start"). Steps 1 to 5 built and checked on the unit, steps 3 to 5 with the owner's Kestrel ([progress](progress.md)). Left: the owner's hands-on check of phone mode; the 34 KB of internal RAM the Bluetooth stack holds while off, and the smaller largest free block after a pull, to look into.

## What we want

A dope card that a PRS shooter can fill in during stage prep and read at a glance on the clock: elevation and wind hold for each target, on the rifle next to the scope. It is filled by hand on the touch screen, or taken from a Kestrel 5700 Elite over Bluetooth, and works fully without one. On the level view the dope and the timer are laid out as one system, in portrait and landscape, with nothing overlapping.

## Owner's direction (2026-11-07)

1. The next ADR is the dope view; first consider the Kestrel integration (`kestrel_5700_connect_library`, the HUD emulator in `private/firmware/kestrel_hud_emulator`).
2. BLE and Wi-Fi share the radio: the radio state needs a careful decision.
3. Consider how a PRS shooter uses this tool.
4. The dope has no windage; add it.
5. Without a Kestrel the dope is entered by hand.
6. A better UI, like the timer's: manual input on the touch screen, and/or values synchronised from the Kestrel.
7. Earlier (ADR-006): the level view's timer widget and the dope strip are designed together, as one system; every proposal shows portrait and landscape; targets physically large enough (9.5 mm, 122 px, the smallest).

## What we know

### How a PRS shooter uses a dope card (research, 2026-11-07)

- Before each stage the shooter learns the targets (a match stage is typically 2 to 6 targets, 300 to 1000 yd; [Precision Rifle Blog](https://precisionrifleblog.com/2023/09/30/how-a-2-time-prs-champ-trues-his-ballistics-calculates-his-dope-at-a-match/) shows a 5-target line to 1000 yd), computes elevation and wind holds with a Kestrel or a phone app, and writes them on a wrist coach, a card on the rifle or tape on the stock ([NSSF](https://www.letsgoshooting.org/resources/articles/shooting-tips/how-to-make-a-dope-card-gathering-long-range-ballistic-data/), [Precision Rifle Blog](https://precisionrifleblog.com/2020/01/05/dope-card-holders-and-heads-up-displays-for-rifle-data/)).
- Elevation is one number per target. Wind is written as a bracket: a two-time PRS champion writes 3 columns (low, average, high wind, 2 to 3 mph apart) and never more than 5, starts on the clock with the average and moves a column after seeing an impact. Others write a minimum and a maximum hold; some none. Light wind or short range: one value ([Snipershide](https://snipershide.com/shooting/threads/capturing-wind-kestrel-5700-elite.7121878)).
- The Kestrel gives exactly a two-value bracket: Wnd1 is the hold for the average wind (WS1) and Wnd2 for the maximum (WS2) of a 5 s wind capture ([Kestrel 5700 Elite manual](https://cdn.shopify.com/s/files/1/0344/1457/files/Kestrel_5700_Elite_Sportsman_Manual.pdf)). Its Target Card (Elite only) holds 10 targets, A to J or 1 to 10, each with its own range, direction and wind.
- On the clock the shooter looks from the scope to the card for each target in the stage's order. This unit sits on the rifle beside the scope, so the card is where the eye already is; the wrist does not move.

### The dope today (`dope_config_view.c`, captures 2026-11-07)

- 12 targets A to L, each one elevation 0.0 to 30.0 (mil assumed, no unit shown) and a show/hide flag. No wind, no range.
- The tile is a list of grey and blue rows in the light theme; a row opens a full-screen message box with two rollers (whole and tenths), an Enable switch and Apply.
- On the level the shown targets sit in a strip along the bottom, 70 x 55 px each, the number in montserrat 32 (digits about 23 px, 1.8 mm, tall). All targets have the same weight; nothing says which one is next.
- The level setting "Auto Move Dope Card on Recoil" exists and is saved, but no code reads it.
- The timer on the level is a 200 x 200 px ring: centred in portrait, at the left in landscape, where it covers the dope strip (280 px tall screen). The tilt box (top centre in portrait, right in landscape) sets the level's zero on a single tap.

### The Kestrel link (`kestrel_5700_connect_library`, ICD 1.18)

- **HUD Connect** (ICD §14, verified with an ESP32 acting as the HUD): with Bluetooth › Conct = Device, the Kestrel is the BLE central and connects out to a HUD it was paired with, and reconnects by itself within about a second of the HUD advertising. The HUD advertises `HUD-<7 digits>` with the `4154…` service and the NK manufacturer data, serves the Kestrel's own LiNK service (`8592…`), ACKs FreeToCommunicate, then authenticates as the host with the link key and talks the full read set.
- So **this unit is the BLE peripheral** and only advertises and answers: no scanning, no pairing code, no bonding. It runs alongside a LiNK rangefinder on the Kestrel (Device Connect is a separate, concurrent link): lase, and the Kestrel's hold for that range reaches the rifle.
- What it reads (all SAFE reads, ICD §6.1):
  - **Live solution** `0x00a0` → `0x00a1`: the selected gun's name, target range, direction of fire, Elv, Wnd1, Wnd2, WS1, WS2, WD, inclination. Holds are always mil, range always metres, whatever the Kestrel displays. The vendor app polls it about once a second.
  - **Target Card** `0x0084` kind 2, then `0x0093 [t][t][ff][00]` for t = 0 to 9 → `0x008a`: range, Elv, Wnd1, Wnd2 for each of the 10 targets of the sector selected on the Kestrel, plus the designator style (A to J or 1 to 10). It changes nothing on the Kestrel.
- Behaviour to design for: the link drops every 2 to 70 s at the bench and reconnects within about 1.5 s (normal for Kestrel links, ICD H-12); each connection authenticates again. While a value is being edited on the Kestrel, the live solution reads NaN (B-6). A disabled gun gives no solution (NACK `0x0c`). With Conct = Device the Kestrel does not advertise, so the phone app cannot connect at the same time.
- The link key is published in the library's ICD (§5.1); both repositories are public.

### Radio and memory (ESP-IDF docs, this unit)

- The ESP32-S3 has one 2.4 GHz radio; ESP-IDF shares it between Wi-Fi and BLE in time slices. Espressif lists BLE advertising or connected with Wi-Fi station (connecting or connected) as supported and stable, and with a SoftAP that has a station connected as supported but **unstable** ([RF coexistence](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/coexist.html)). This unit's Wi-Fi provisioning is SoftAP (`network_provisioning/scheme_softap.h`), with the phone connected to it.
- So BLE and Wi-Fi do not exclude each other; the costs are internal RAM, radio time during heavy Wi-Fi traffic (OTA download), and the one unstable case, provisioning.
- Today, Wi-Fi on and connected: free internal RAM 181 KB, lowest since boot 145 KB, largest block 100 KB (`mem` at 15:17); app 2.0 MB of the 4 MB partition. Espressif's examples put a BLE stack at tens of KB of internal RAM, NimBLE smaller than Bluedroid but varying with its buffer settings ([memory comparison](https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/advanced-development/system/ram-usage.html)). The cost on this build is not measured yet; step 3 measures it before anything depends on it.

## What we will do, and why

### The dope data (one model for hand entry and the Kestrel)

10, 12 (default) or 15 targets, the **Card Size** setting on the Dope page (owner, 2026-11-07: PRS stages run to 12). The Kestrel's Target Card holds 10, so a pull fills only the first 10; targets past 10 are entered by hand. Each target: elevation (mil, 0.1 steps, signed: U/D), wind hold (mil, signed: L/R), gust hold (optional, the bracket's high end, Wnd2), range (optional, shown when known), and where it came from (hand or Kestrel). A target with no elevation is empty and not shown on the level. The saved block changes size, so `DOPE` starts empty once (resets accepted for features under review).

The current target on the level is RAM only: stepping through targets on every shot must not write NVS.

Mil only, as the Kestrel sends; MOA is out of scope.

### The dope tile (setup, between stages)

- A list of the card's targets (10, 12 or 15), dark like the timer tile. Each row: the letter and range, an **Elevation** cell and a **Wind** cell, each a tap target of at least 122 x 122 px. Empty targets show "–".
- Tap a cell: the **value editor**, the timer editor's layout and rules reused (value, minus, plus, Done; press steps 0.1, hold repeats and speeds up). Wind steps through zero from L to R, so there is no separate direction control. Done saves only after a change (ADR-006 rule). Editing the wind by hand clears the gust (the bracket was the Kestrel's).
- Long press a row: clear that target. A "Clear all" row at the end for the next stage, which asks once.
- Targets are named A, B, C... or 1, 2, 3..., a setting on a **Dope** page in the settings menu, as the Kestrel's Target Card offers (owner, 2026-11-07).
- When the Kestrel is set up, a Kestrel row at the top with **Pull targets**, which replaces the card's first 10 targets with the Kestrel's and saves once; targets past 10 stay. Pull is a deliberate action, never automatic, so a hand edit is never overwritten behind the shooter's back.

### The level view: dope and timer as one system

The level is the primary feature, and the view must be read at a glance (owner, 2026-11-07). The layout came out of several rounds with the owner and a look at how small glanceable screens are designed (2026-11-07):
- A watch face exists to tell the time; everything else sits in small fixed places around it. Google's rules: glanceable, simple layout, black, inside the bezel; about 7 s of attention, so test while moving ([Wear OS watch faces](https://developer.android.com/design/wear/watchfaces), [tiles](https://developer.android.com/design/ui/wear/guides/surfaces/tiles/bestpractices), [wearables](https://developer.android.com/design/ui/wear/guides/get-started/design-for-wearables)).
- Aviation HUDs: symbols that cover the scene are the routine complaint; keep symbols few and the horizon always visible ([SKYbrary](https://www.skybrary.aero/articles/head-display-hud)).
- Peripheral vision picks up colour, not text ([PeriphAR](https://arxiv.org/html/2603.18350v1)). PRS anti-cant levels work that way: MDT's Send iT is a row of LEDs, the centre one meaning level, 0.6° by default ([MDT](https://mdttac.ca/send-it-gen2-level)); SIG's LevelPlex lights an arrow on the canted side ([Gun Digest](https://gundigest.com/?p=244905)). The level view's full-screen colour field already does this.

The layout, the same in both rotations:
- **The level stays as it is**: the colour field fills the screen, the horizon and the edge ticks in the middle. When level, **the whole screen** turns the level colour, under the text as well.
- **The tilt box** stays an outlined box, because it says "button" (owner): centred at the top in both rotations, digits centred with the sign hung to the left so the number does not shift. A tap shows "Hold to set zero"; holding sets the mount offset, so a touch on the clock cannot move the zero.
- **A band** from 20 px below the ticks to the bottom edge holds the timer and the dope, with no background: white text reads on every level colour on the AMOLED (owner, 2026-11-07, after using it; a light black overlay was built first and removed).
- **The timer** is a bar across the top of the band: its length is the time left (orange; red in the last 10 s; grey when paused), the digits at the right of the dope's first line. While armed, a tap anywhere above the band starts it: the largest target on the screen for the moment of the beep. While running, only the time row pauses it.
- **The dope**: the target letter, range and next target on the first line; the elevation as the largest text on the screen; the wind and, when known, the gust after it. Portrait stacks elevation over wind (elevation digits about 84 px tall, 6.5 mm); landscape puts them on one row (about 60 px). Larger than montserrat 48 needs a digits-only font, a few KB of flash.
- **Switching targets is manual first** (owner, 2026-11-07: recoil detection is unreliable with .22LR and heavy rifles, where handling makes more noise than the shot): tap the band for the next target, hold for the previous. Moving on recoil stays as the existing setting, now built, **off by default**. After the last target it stays there.
- Without a card and an armed timer there is no band: the level alone, as today.
- 20 px between elements, 8 px from the panel edge, nothing in the 30 px corners.

### The Kestrel pull (one-off, on demand)

How the Kestrel is used at a match (owner, 2026-11-07): the shooter ranges the targets, works out the dope on the Kestrel, and often switches the Kestrel off during the match. So there is no continuous link and no live view: **the pull is stateless and one-off**. The gun profile's name is not read (one rifle per match).

- **Bluetooth is off by default.** Pull starts the BLE stack, connects, authenticates, reads the Target Card's targets, disconnects and shuts the stack down again. The BLE stack takes RAM only during a pull.
- The card is replaced only when every target has arrived; a failed or cancelled pull leaves it unchanged. Progress on screen ("Reading target 4/10"); clear failures ("Kestrel not found", "Kestrel busy", "Pull failed, card unchanged").
- Unused Target Card targets: how they look in the pull is not known yet; step 3 finds out on the owner's Kestrel, so that Pull brings in only the targets that were set up.
- **Two connection modes, a setting** (owner, 2026-11-07); the protocol, authentication and reads are the same over both (the library's client already runs over either transport):
  - **HUD** (default; the owner's match setup, Kestrel in Device mode for a LiNK rangefinder): the unit advertises as the Kestrel's HUD and the Kestrel connects to it. Set up once on the Kestrel: Bluetooth › Conct = Device › HUD Connect › Name › New, pick `HUD-` and 7 digits (from the unit's MAC, so it stays the same). Whether the Kestrel holds the rangefinder and the HUD links at once is from the manual, not yet seen; checked with the owner's rangefinder if available.
  - **Phone** (Kestrel in its normal mode): the unit connects to the Kestrel as the phone app does. Set up once on the unit: scan, pick the Kestrel, which is remembered by serial (a firing line has many Kestrels). Fails while the phone app is connected or the Kestrel is in Device mode; the message says so.
- **The radio rule** is now short: Pull is refused during Wi-Fi provisioning (Espressif's unstable combination with BLE) and OTA; it is allowed with Wi-Fi connected (stable); the screen stays awake while it runs.
- **Profiled live** (owner, 2026-11-07: the Kestrel can be slow on the first connection): every phase of a pull is timed and logged (stack start, until the Kestrel connects or is found, discovery, authentication, each target read, disconnect, stack stop), on the owner's Kestrel in both modes. If the first connection is the slow part, the data decides whether to start Bluetooth when the dope tile opens, so the link is ready when Pull is pressed.

Built as a service like Wi-Fi (ADR-005): its own task, events to the UI bridge, no LVGL. NimBLE, one connection, peripheral (HUD) and central (phone) roles. The protocol layer (framing, CRC, AES-128, the host-side authentication, the reads) is a component with no dependency on the rest of the firmware, written from the ICD and checked against its test vectors, so it can move into the Kestrel library later.

A **Kestrel** settings page, grouped like the Timer page: the mode (off, HUD, phone; default off), the HUD name to pick on the Kestrel or the Kestrel picked by scan, the last pull's result and timing, and the setup steps for the chosen mode.

## Steps

Each step is built, flashed and checked on the unit (captures in both rotations, `snap`) before the next.

1. **The dope data, the tile and the value editor**, by hand. Check: enter a stage (5 targets, elevation and wind) by touch; Done saves once; leaving saves nothing; reboot keeps it.
2. **The level view's band, timer bar and dope**, the large digits, the start tap above the band, manual next and previous, the recoil setting (off by default), the tilt box. Check: captures in both rotations, canted and level, with the timer armed, running and paused and a 5-target card; 20 px gaps and nothing in the corners; the owner by hand.
3. **The Kestrel pull in HUD mode**, with profiling, reading into the log and the Kestrel page first. Check with the owner's Kestrel: setup on the Kestrel, pulls with the timing of every phase, what unused targets look like; internal RAM before, during and after five pulls in a row (pass: the lowest stays above 60 KB during a pull with Wi-Fi connected, and after the pulls free RAM is back where it started); Pull refused during provisioning.
4. **Pull into the card**, from the dope tile. Check: a Target Card sector on the Kestrel arrives on the unit with the same holds as the Kestrel shows; a pull cut off halfway (Kestrel switched off) leaves the card unchanged.
5. **Phone mode**: scan, pick and remember the Kestrel, the same pull. Check: as step 4, with the Kestrel in its normal mode, profiled the same way.

## Owner's decisions (2026-11-07)

1. Radio: both built in, with a rule per unit state, not a switch (refined the same day: with a one-off pull, the rule is only "no pull during provisioning or OTA").
2. Wind: a hold and an optional gust per target (not one hold only).
3. Kestrel data: Pull on demand, stateless and one-off; no live view and no continuous link (revised the same day after discussion; the Live entry is dropped).
4. Scope: this one ADR, hand entry first (not two ADRs).
5. The level: the colour field stays full screen and turns fully green (the configured level colour) when level; the timer bar and the dope sit on a light band at the bottom; the tilt box stays a box, centred at the top; manual target switching first, recoil off by default; no gun name.
6. Connection: HUD mode first, phone mode as a setting.

Raised in the discussion, not in this ADR: the timer's start. A button at the beep suits PRS and field matches; the first shot starts late (finding the target takes about 10 s) and is unreliable with .22LR and heavy rifles. The owner suggests the muzzle's direction as the trigger: the rifle moving off its start position after the beep. That is a timer change (ADR-006's area) and gets its own ADR after this one, starting from recorded orientation data of real stage starts.

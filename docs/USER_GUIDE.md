# JAYCEE Lottery User Guide

This guide covers installation, event preparation, live drawing, the audience display, results, and local data management.

## 1. Install or run the app

Choose one release from the `dist` folder:

- `JAYCEE-Lottery-Setup.exe` installs the app for the current Windows user. Administrator access is not required.
- `JAYCEE Lottery.exe` is the standalone portable application.
- `JAYCEE-Lottery-portable.zip` contains the portable app, documentation, and CSV template.

The installer can create a desktop shortcut and always adds Start Menu and uninstall entries. Because the installer is not code-signed, Windows SmartScreen may show an unknown-publisher warning.

## 2. Prepare an event

### Numeric coupon mode

Numeric mode is active when no participant CSV is loaded.

1. Open **Draw**.
2. Set **Winners** to the number of candidates needed in one draw.
3. Set **Total Coupons** to the highest coupon number in the pool.
4. Leave **No repeat pool** off when numbers may win again, or turn it on to exclude confirmed winners.

### Participant mode

Open **Participants** and choose **Import CSV**. A valid participant list immediately replaces numeric mode and controls the pool size. Replacing or clearing the list resets the no-repeat pool.

Use the included `participants-template.csv` as a starting point. See [Participant CSV Format](CSV_FORMAT.md) for accepted headers and eligibility groups.

### Configure prizes

Open **Prizes** to:

- add or rename prizes;
- choose the active prize;
- set a default winner count for each prize;
- restrict a prize to a participant group; or
- remove unused prizes.

At least one prize always remains. Group eligibility is available when a participant CSV contains group values.

## 3. Run a draw

1. Select the active prize and winner count.
2. Choose **Draw Winners** or press `Space`.
3. Wait for the draw animation to finish.
4. Review the candidate numbers or participant names.
5. Select **Confirm Winners** or press `Space` to save the result.
6. Select **Redraw** if the candidates should be discarded and drawn again.

Only confirmed candidates are written to History and the no-repeat pool. A redraw does not consume those candidates.

If the requested winner count is larger than the eligible pool, reduce the count, change the prize eligibility, or reset the no-repeat pool.

## 4. Use the no-repeat pool

When **No repeat pool** is on, every confirmed number or participant is excluded from future draws. The remaining count appears on the Draw page.

Open **History** and choose **Reset Pool** to make confirmed entries eligible again. Resetting the pool does not erase the History list. Likewise, clearing History does not reset the pool; the two operations are deliberately separate.

## 5. Present to an audience

1. Connect the projector or second monitor before opening the audience display.
2. Open **Show**.
3. Optionally enter an event title.
4. Choose an accent theme and enable or disable reveal sound and confetti.
5. Keep **Liquid motion** enabled for ambient lighting, soft glass hover feedback, eased scrolling, spring transitions, and cinematic winner reveals; disable it for a calmer static interface.
6. Select **Open Audience Display**.

On a second monitor, the audience display opens fullscreen automatically. With one monitor, it opens as a resizable preview. The presentation rotates welcome, next-prize, and pool slides until a draw begins, then progresses through countdown, animation, reveal, and confirmed summary.

## 6. Scale and scroll the interface

Use the **Interface Scale** control on the Show page or these shortcuts:

- `Ctrl++`: increase scale;
- `Ctrl+-`: decrease scale;
- `Ctrl+0`: return to 100%.

The range is 75% to 135%. When the scaled workspace is taller than the window, a slim scroll indicator appears at the right edge. Use the mouse wheel to move the page with eased inertia. When the pointer is over a long participant, result, or history list, the wheel scrolls that list first.

The **Liquid motion** switch on the Show page pauses ambient animation, scroll easing, and transitions while retaining the interface's static depth and hierarchy. No light follows the mouse pointer. Animation timing adapts to the display refresh rate, so the same transitions remain smooth and similarly paced on standard and high-refresh monitors.

## 7. Review and export results

Open **History** to see every confirmed draw. Choose **Export CSV** and select a save location. The UTF-8 report includes prize, winner information, timestamp, and draw mode and can be opened in Excel or Google Sheets.

Choose **Clear History** only when archived draw records are no longer needed. Export first if a permanent event record is required.

## 8. Back up or move event data

JAYCEE Lottery saves automatically to:

```text
%LOCALAPPDATA%\JAYCEE Lottery\lottery_state.txt
```

To make a backup, close the application and copy `lottery_state.txt` to a safe location. To move the state to another computer, install or copy the application there, launch it once, close it, and replace the newly created state file with the backup.

The portable executable still uses this Local AppData location; its data is not stored beside the executable.

## 9. Uninstall

Use **Windows Settings → Apps → Installed apps → JAYCEE Lottery**, or use the uninstall shortcut in the JAYCEE Lottery Start Menu folder.

Uninstall removes the program and shortcuts but keeps `lottery_state.txt` to protect event records. Delete that file manually only when the saved data is no longer needed.

## Troubleshooting

### Windows blocks the installer

The current installer is not digitally signed. Confirm that it came from the project repository before choosing the Windows option to run it. A future signed release would remove the unknown-publisher warning.

### Participant names do not appear

Confirm that the file is UTF-8 CSV, each data row has a non-empty name, and the first row uses recognized headers. Start from the included template.

### There are not enough eligible winners

Reduce the winner count, select an unrestricted prize, add eligible participants, or reset the no-repeat pool.

### The audience display opens on the wrong screen

Close the audience window, make sure the projector is connected and configured as an extended display in Windows, then open the audience display again.

### The interface does not fit

Press `Ctrl+-`, scroll with the mouse wheel, or press `Ctrl+0` to restore 100% scale.

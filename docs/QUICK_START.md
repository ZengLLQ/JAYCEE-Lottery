# JAYCEE Lottery Quick Start

JAYCEE Lottery is a native 64-bit Windows event drawing and audience-presentation application. It does not require Python, Qt, a browser runtime, or another interpreter.

## Start a numeric draw

1. Open **Draw**.
2. Set the number of **Winners** and **Total Coupons**.
3. Choose **Draw Winners** or press `Space`.
4. Review the candidates, then confirm or redraw.

## Draw participant names

Open **Participants**, choose **Import CSV**, and select `participants-template.csv` or your own UTF-8 CSV with these columns:

```csv
ticket,name,group
1001,Alyssa Hart,Marketing
1002,Benjamin Lee,Operations
```

Imported participants replace numeric mode. The optional group column can restrict prize eligibility.

## Present to an audience

Open **Show**, customize the theme and effects, then select **Open Audience Display**. The window opens fullscreen on a second monitor when one is available.

## Useful controls

- `Space`: start a draw or confirm candidates
- `N`: toggle the no-repeat pool
- `Ctrl+D`, `Ctrl+I`, `Ctrl+P`, `Ctrl+S`, `Ctrl+H`: navigate pages
- `Ctrl++`, `Ctrl+-`, `Ctrl+0`: change or reset interface scale
- Mouse wheel: scroll the page or list under the pointer
- `F11`: toggle fullscreen
- `Esc`: close a dialog or leave fullscreen

## Results and saved data

Use **History → Export CSV** to save a winner report.

The application saves its state at:

```text
%LOCALAPPDATA%\JAYCEE Lottery\lottery_state.txt
```

Uninstalling the application keeps this state file to protect event records.

The installer is not digitally signed, so Windows SmartScreen may show an unknown-publisher warning.

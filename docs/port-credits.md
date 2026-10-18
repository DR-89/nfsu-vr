# Android port credits

On Android, the Options selector's `GameTrailers` button is captioned
`ANDROID PORT` and opens a native Android dialog. The dialog uses the launcher
logo, card and button resources, with Russian/English text, clickable author,
repository, upstream-port and build-commit links. Build identity uses the same
`BuildConfig.BUILD_COMMIT` / `BUILD_COMMIT_FULL` values as the launcher.

The retail selector handler at `0x000C1FD0` receives four stack arguments.
`0x0C407210` is native activation; argument two is the selected FEng object,
whose name hash at `+0x10` is `0x8816789F` for `GameTrailers`. Only that event
is consumed (including its `ret 16` stack cleanup), leaving the selector open.
All other events use the generated handler. Its initialization event
`0x35F8620B` is followed by a caption update through the native text setter,
including text children when the button is a group. Non-Android retains retail
behavior. No game files are modified.

Device checks: open Options, verify ANDROID PORT below CREDITS, activate by
controller and touch; verify each link in a browser, return to the game, close
with the button and Android Back, and reopen. Check that CREDITS and adjacent
options still work, and that no activation leaks through the dialog.

/* SPDX-License-Identifier: GPL-2.0-or-later */
import javax.microedition.lcdui.Command;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Displayable;

public final class GnuboyLauncher extends HomebrewMIDlet {
    private final Command chooseCommand = new Command("Select ROM", Command.SCREEN, 1);
    private String selected = "";
    protected String applicationName() { return "Gnuboy"; }
    protected String startLabel() { return "Start Gnuboy"; }
    protected String nativeArgument() { return selected; }
    protected void configureCommands(Displayable target, boolean enabled) {
        if (enabled) target.addCommand(chooseCommand);
        else target.removeCommand(chooseCommand);
    }
    public void commandAction(Command command, Displayable target) {
        if (command == chooseCommand) {
            HomebrewFilePicker.show(Display.getDisplay(this), target, "C:/predefgallery/", "Select ROM",
                new String[] {".gb", ".gbc"}, new HomebrewFilePicker.Listener() {
                    public void selected(String path) { selected = path; refreshInstructions(); }
                });
        } else super.commandAction(command, target);
    }
    protected String[] instructions(int layout) {
        return new String[] {"GAME BOY / GAME BOY COLOR", "Select ROM: Gallery / Received files",
            selected.length() == 0 ? "or Start for the native game list" : selected.substring(selected.lastIndexOf('/') + 1),
            "D-pad: move; 1 / 3: A / B", "5: Start; 0: Select; center: A",
            "*: pause / saves; #: scale", "Pause: 7 sound; 2 / 8 volume",
            "Battery saves stay beside the ROM"};
    }
}

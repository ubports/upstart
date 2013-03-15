#!/usr/bin/python3
# -*- coding: utf-8 -*-
# vim: set fileencoding=utf-8
#---------------------------------------------------------------------
# Copyright © 2013 Canonical Ltd.
#
# Author: James Hunt <james.hunt@canonical.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2, as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#---------------------------------------------------------------------
# TODO:
#
# - misc
#   - handle_clear_data() default message should use gettext
# - gui
#   - Gtk.STOCK_MEDIA_PLAY toggle
#   - menus
#       - Edit->Copy
#       - Edit->Select
#       - Edit->Select All
#       - Edit->Copy All
#       - Connection
#         - Choose from 4 connection types.
#       - View
#         - Show/Hide index+time columns
#         - Pause of auto-scroll
#---------------------------------------------------------------------

import os
import sys
import gettext
import argparse
import signal
import dbus
import dbus.mainloop.glib
from datetime import datetime

VERSION = 0.1
COPYRIGHT = 'Copyright © 2013 Canonical Ltd.'
DESCRIPTION = 'Simple Upstart Event Monitor'
NAME = 'upstart-monitor'
AUTHORS = [
    'James Hunt <james.hunt@ubuntu.com>'
]
WEBSITE = 'http://upstart.ubuntu.com'

cli = False

try:
    from gi.repository import Gtk, Gdk, GdkPixbuf, GLib
except ImportError:
    gettext.install(NAME)
    print("%s: %s" % (_('WARNING'), _('GUI modules not available - falling back to CLI')), file=sys.stderr)
    cli = True

"""
Simple command-line and GUI event monitor for Upstart.
"""

# If this file does not exist or is not readable...
LICENSE_FILE = 'aa/usr/share/common-licenses/GPL-2'

# ... display this text instead.
LICENSE = """
http://www.gnu.org/
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2, as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
"""

#------------------------------

DEFAULT_WIN_SIZE_WIDTH  = 600
DEFAULT_WIN_SIZE_HEIGHT = 200

DEFAULT_LOGO_SIZE_WIDTH  = 150
DEFAULT_LOGO_SIZE_HEIGHT = 150

DEFAULT_OUTPUT_FILE = 'upstart-events.txt'

# key=type : value=description
destinations = \
{
    'system-bus'     : 'D-Bus system bus',
    'session-bus'    : 'D-Bus session bus',
    'system-socket'  : 'D-Bus system socket',
    'session-socket' : 'D-Bus session socket'
}

#------------------------------

# well-known address for Upstart running as PID 1
INIT_SOCKET = 'unix:abstract=/com/ubuntu/upstart'

# Address of Session Inits private socket
SESSION_SOCKET = os.environ.get('UPSTART_SESSION')

UI_INFO = """
<ui>
    <menubar name='MenuBar'>

        <menu action='FileMenu'>
            <menuitem action='FileNew'/>
            <menuitem action='FileSave'/>
            <menuitem action='FileSaveAs'/>
            <separator/>
            <menuitem action='FileQuit'/>
        </menu>

<!--
        <menu action='EditMenu'>
            <menuitem action='EditCopy'/>
        </menu>

        <menu action='OptionsMenu'>
            <menuitem action='OptionsAutoScroll'/>
            <menu action='ConnectionMenu'>
                <menuitem action='System Bus'/>
                <menuitem action='System Socket'/>
                <menuitem action='Session Bus'/>
                <menuitem action='Session Socket'/>
            </menu>
        </menu>

-->
        <menu action='HelpMenu'>
            <menuitem action='HelpAbout'/>
        </menu>

    </menubar>

    <popup name='ContextMenu'>
        <menuitem action='ContextCopy'/>
    </popup>
</ui>
"""


def format_event(*args):
    """
    Format raw D-Bus event details.

    Returns: event and quoted event environment tuple.
    """
    quoted_env = []

    event = args[0]

    # unfortunately, quotes are stripped so they need to be re-added
    # to preserve whitespace in the environment variable values.
    raw_env = args[1]

    for e in raw_env:
        f = e.split('=')
        quoted_env.append("%s='%s'" % (f[0], ''.join(f[1:])))

    env = ' '.join(str(e) for e in quoted_env)

    return event, env


def cmdline_event_handler(*args):
    """
    format event data for command-line display
    """
    global cmdline_args

    event, env = format_event(*args)
    now = datetime.now().strftime("%F %T.%f")
    event_str = "%s %s" % (event, env) if env else event
    sep = cmdline_args.separator if cmdline_args.separator else "\t"
    print("%s%s%s" % (now, sep, event_str))


class UpstartEventsGui(Gtk.Window):
    """
    GUI Upstart Event monitor.
    """
    global bus
    global cmdline_args

    auto_scroll = True
    need_save = False

    # value of cell containing event details that user has
    # right-clicked over
    context_value = None

    def add_row(self, event, event_env):
        """
        Add new row to view.
        """
        self.data_index += 1
        now = datetime.now().strftime("%F %T.%f")
        event_str = "%s %s" % (event, event_env) if event_env else event

        row = [self.data_index, now, event_str]
        self.liststore.append(row)


    def event_handler(self, *args):
        """
        Upstart has emitted an event so create a new row in the view.
        """
        event, env = format_event(*args)
        self.add_row(event, env)

        # New data arrived since last save (if any).
        self.need_save = True


    def register_cb(self):
        """
        Callback to register a D-Bus callback whenever Upstart emits an
        event.
        """
        # register a D-Bus handler
        bus.add_signal_receiver(self.event_handler,
        dbus_interface='com.ubuntu.Upstart0_6',
        signal_name="EventEmitted")

        # deregister this callback
        return False


    def treeview_changed(self, widget, event, data=None):
        """
        Allow GUI to auto-scroll.
        """
        adj = self.scrolled_window.get_vadjustment()
        adj.set_value(adj.get_upper() - adj.get_page_size())


    def ask_question(self, msg):
        """
        Ask a question.

        @msg: message to display.

        Returns: True if user answered positively, else False.
        """
        dialog = Gtk.MessageDialog(self,
                Gtk.DialogFlags.DESTROY_WITH_PARENT,
                Gtk.MessageType.QUESTION,
                Gtk.ButtonsType.YES_NO,
                msg)
        response = dialog.run()
        dialog.destroy()
        return True if response == Gtk.ResponseType.YES else False


    def show_dialog(self, msg):
        """
        Display an "ok" dialog which the user must click.

        @msg: message to display.
        """
        dialog = Gtk.MessageDialog(self,
                Gtk.DialogFlags.DESTROY_WITH_PARENT,
                Gtk.MessageType.ERROR,
                Gtk.ButtonsType.OK,
                msg)
        dialog.run()
        dialog.destroy()


    def show_about(self):
        """
        Show an About dialog.
        """
        about = Gtk.AboutDialog()
        about.set_program_name(NAME)
        about.set_version("%s %s" % (_('Version'), VERSION))
        about.set_copyright(COPYRIGHT)
        about.set_comments(DESCRIPTION)
        about.set_authors(AUTHORS)
        about.set_website(WEBSITE)

        try:
            with open(LICENSE_FILE, 'r') as f:
                license = f.read()
        except IOError:
                license = LICENSE

        about.set_license(license)
        about.set_logo(self.icon_pixbuf)
        about.run()
        about.destroy()


    def on_button_clear_clicked(self, widget):
        """
        Clear cached event data (also clears GUI window).
        """
        if self.handle_clear_data():
            self.liststore.clear()
            self.data_index = 1
            self.need_save = False

    def on_button_pause_clicked(self, widget):
        """
        Toggle auto-scroll.
        """
        if self.auto_scroll:
            self.treeview.disconnect(self.auto_scroll_handler)
            self.auto_scroll = False
        else:
            self.auto_scroll_handler = \
            self.treeview.connect('size-allocate', self.treeview_changed)
            self.auto_scroll = True


    def save_data(self, path):
        """
        Write received event data to specified path.
        """
        try:
            fh = open(path, 'w')
        except IOError:
            self.show_dialog('%s: %s' % (_('Error saving file'), path))

        for row in self.liststore:
            fh.write("%d\t%s\t%s\n" % (row[0], row[1], row[2]))
        fh.close()

        # All data was written
        self.need_save = False


    def handle_save(self, filename=None):
        """
        Display a file chooser dialog to allow user to select a filename to
        hold the cached event data.
        """
        # no data yet
        if not len(self.liststore):
            self.show_dialog(_('No events to save'))
            return

        if filename != None:
            path = filename
            self.save_data(path)
            return

        file_chooser = Gtk.FileChooserDialog(_('Save File'), self.get_toplevel(),
        Gtk.FileChooserAction.SAVE,
        (Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL,
        Gtk.STOCK_SAVE, Gtk.ResponseType.ACCEPT))

        file_chooser.set_current_name(DEFAULT_OUTPUT_FILE)

        response = file_chooser.run()

        if response != Gtk.ResponseType.ACCEPT:
            file_chooser.destroy()
            return

        path = file_chooser.get_filename()

        self.save_data(path)
        file_chooser.destroy()

    def handle_quit(self):
        """
        Quit application; if unsaved data exists, prompt user.
        """

        if self.handle_clear_data('Quit without saving?'):
            Gtk.main_quit()

    def on_button_quit_clicked(self, widget):
        """
        Handle quit request.
        """
        self.handle_quit()


    def on_button_save_clicked(self, widget):
        """
        Handle save request.
        """
        self.handle_save()

    # FIXME: default message should use gettext
    def handle_clear_data(self, message='Clear data without saving?'):
        """
        Handle potentially clearing of cached events.

        Returns: True if user is happy to clear the data, else False.
        """
        if len(self.liststore) and self.need_save:
            answer = self.ask_question(message)
            return answer
        else:
            # no data would be lost so allow action
            return True

    def activate_action(self, action, user_data=None):
        """
        Main menu-callback-handling method.
        """
        name = action.get_name()

        if name == 'FileNew':
            if self.handle_clear_data():
                self.liststore.clear()
                self.data_index = 1
                self.need_save = False
        elif name == 'FileSave':
            self.handle_save(DEFAULT_OUTPUT_FILE)
        elif name == 'FileSaveAs':
            self.handle_save()
        elif name == 'FileQuit':
            self.handle_quit()
        elif name == 'HelpAbout':
            self.show_about()
        elif name == 'ContextCopy':
            # Save event details from cell text to clipboard
            clipboard = Gtk.Clipboard.get(Gdk.SELECTION_CLIPBOARD)
            clipboard.set_text(self.context_value, -1)

    def create_ui_manager(self):
        """
        Errrm... create the UI Manager.
        """
        ui_manager = Gtk.UIManager()

        # Throws exception if something went wrong
        ui_manager.add_ui_from_string(UI_INFO)

        # Add the accelerator group to the toplevel window
        accelgroup = ui_manager.get_accel_group()
        self.add_accel_group(accelgroup)
        return ui_manager

    def on_button_press_event(self, treeview, event):
        """
        Handle mouse button press events.
        """
        if event.type == Gdk.EventType.BUTTON_PRESS and event.button == 3:
            x = int(event.x)
            y = int(event.y)
            path_info = treeview.get_path_at_pos(x, y)
            if path_info:
                path, col, cell_x, cell_y = path_info

            model = treeview.get_model()
            model_iter = model.get_iter(path)

            # Save the event details from the selected cell
            # (event is 3rd column (zero-indexed)).
            self.context_value = model.get_value (model_iter, 2)

            treeview.grab_focus()
            treeview.set_cursor(path, col, 0)

            # Display context menu
            self.popup.popup(None, None, None, None, event.button, event.time)
            return True


    def __init__(self):
        """
        Setup.
        """
        Gtk.Window.__init__(self, title=DESCRIPTION)

        main_menu_action_entries = (
            ('FileMenu', None, 'File'),
            ('FileNew', Gtk.STOCK_NEW, 'New', '<control>N',
                _('Clear events'), self.activate_action),
            ('FileSave', Gtk.STOCK_SAVE, 'Save', '<control>S',
                _('Save events to default file'), self.activate_action),
            ('FileSaveAs', Gtk.STOCK_SAVE_AS, 'Save As', None,
                _('Save events to specified file'), self.activate_action),
            ('FileQuit', Gtk.STOCK_QUIT, 'Quit', '<control>Q',
                _('Exit application'), self.activate_action),

            ('HelpMenu', None, 'Help'),
            ('HelpAbout', Gtk.STOCK_HELP, 'About', '<control>A',
                _('Application details'), self.activate_action),
        )

        context_menu_action_entries = (
            ('ContextCopy', Gtk.STOCK_COPY, 'Copy', '<control>C',
                _('Copy'), self.activate_action),
        )

        self.data_index = 0

        self.set_default_size(DEFAULT_WIN_SIZE_WIDTH, DEFAULT_WIN_SIZE_HEIGHT)
        self.set_resizable(True)

        theme = Gtk.IconTheme.get_default()
        self.icon_pixbuf = theme.load_icon(NAME, 128, Gtk.IconLookupFlags.GENERIC_FALLBACK)
        self.set_icon(self.icon_pixbuf)

        action_group = Gtk.ActionGroup('UpstartMonitorActions')
        action_group.add_actions(main_menu_action_entries)
        action_group.add_actions(context_menu_action_entries)

        ui_manager = self.create_ui_manager()
        ui_manager.insert_action_group(action_group)

        menubar = ui_manager.get_widget("/MenuBar")

        # data types we'll be using in each column
        self.liststore = Gtk.ListStore(int, str, str)

        GLib.idle_add(self.register_cb)

        self.box = Gtk.Box(spacing=6, orientation=Gtk.Orientation.VERTICAL)

        self.scrolled_window = Gtk.ScrolledWindow()
        self.scrolled_window.set_policy(Gtk.PolicyType.ALWAYS, Gtk.PolicyType.ALWAYS)

        self.treeview = Gtk.TreeView(model=self.liststore)

        # columns are zero-based
        self.treeview.set_search_column(2)

        self.treeview.set_headers_clickable(True)

        if self.auto_scroll:
            self.auto_scroll_handler = \
            self.treeview.connect('size-allocate', self.treeview_changed)

        renderer_text = Gtk.CellRendererText()

        # XXX: mark column as editable, but do NOT connect the 'edited'
        # signal. This allows the user to select the text and copy it,
        # but they cannot modify it.
        renderer_editabletext = Gtk.CellRendererText()
        renderer_editabletext.set_property('editable', True)

        column_index = Gtk.TreeViewColumn(_('Index'), renderer_text, text=0)
        column_index.set_resizable(True)
        self.treeview.append_column(column_index)

        column_time = Gtk.TreeViewColumn(_('Time'), renderer_text, text=1)
        column_time.set_resizable(True)
        self.treeview.append_column(column_time)

        column_event = Gtk.TreeViewColumn(_('Event and environment'),
            renderer_editabletext, text=2)
        column_event.set_resizable(True)

        self.popup = ui_manager.get_widget("/ContextMenu")
        self.treeview.connect('button-press-event', self.on_button_press_event)

        self.treeview.append_column(column_event)

        # stops column titles from scrolling along with the data
        self.scrolled_window.add(self.treeview)

        self.button_clear = Gtk.Button(stock=Gtk.STOCK_CLEAR)
        self.button_clear.connect('clicked', self.on_button_clear_clicked)

        # FIXME: toggle to Gtk.STOCK_MEDIA_PLAY
        self.button_pause = Gtk.Button(stock=Gtk.STOCK_MEDIA_PAUSE)
        self.button_pause.connect('clicked', self.on_button_pause_clicked)

        self.button_save = Gtk.Button(stock=Gtk.STOCK_SAVE)
        self.button_save.connect('clicked', self.on_button_save_clicked)

        self.button_quit = Gtk.Button(stock=Gtk.STOCK_QUIT)
        self.button_quit.connect('clicked', self.on_button_quit_clicked)

        self.label_connected = Gtk.Label('%s %s' % (_('Connected to'),
            destinations[cmdline_args.destination]))
        self.label_connected.set_justify(Gtk.Justification.RIGHT)

        self.buttons_box = Gtk.Box(spacing=6, orientation=Gtk.Orientation.HORIZONTAL)
        self.buttons_box.pack_start(self.button_clear, False, True, 0)
        self.buttons_box.pack_start(self.button_pause, False, True, 0)
        self.buttons_box.pack_start(self.button_save, False, True, 0)
        self.buttons_box.pack_start(self.button_quit, False, True, 0)
        self.buttons_box.pack_start(self.label_connected, True, True, 0)

        self.box.pack_start(menubar, False, False, 0)
        self.box.pack_start(self.scrolled_window, True, True, 0)
        self.box.pack_end(self.buttons_box, False, False, 0)
        self.add(self.box)


    def text_edited(self, widget, path, text):
        self.liststore[path][1] = text


def main():
    """
    Parse arguments and run either the command-line or the GUI monitor.
    """
    global bus
    global cmdline_args
    global cli

    gettext.install(NAME)

    parser = argparse.ArgumentParser(description=_('Upstart Event Monitor'))

    parser.add_argument('-n', '--no-gui',
            action='store_true',
            help=_('run in command-line mode'))

    parser.add_argument('-s', '--separator',
            help=_('field separator to use for command-line output'))

    parser.add_argument('-d', '--destination',
            choices=destinations.keys(),
            help=_('connect to Upstart via specified D-Bus route'))

    cmdline_args = parser.parse_args()

    # allow interrupt
    signal.signal(signal.SIGINT, signal.SIG_DFL)

    dbus.set_default_main_loop(dbus.mainloop.glib.DBusGMainLoop())

    if not cmdline_args.destination:
        # If no destination specified, attempt to connect to session
        # if running under a Session Init, else connect to the system
        # bus (since this allows even non-priv users to see events).
        if SESSION_SOCKET:
            cmdline_args.destination = 'session-socket'
        else:
            cmdline_args.destination = 'system-bus'

    if cmdline_args.destination == 'system-bus':
        bus = dbus.SystemBus()
    elif cmdline_args.destination == 'session-bus':
        bus = dbus.SessionBus()
    elif cmdline_args.destination == 'system-socket':
        socket = INIT_SOCKET
        bus = dbus.connection.Connection(socket)
    elif cmdline_args.destination == 'session-socket':
        socket = SESSION_SOCKET
        bus = dbus.connection.Connection(socket)
    if cmdline_args.no_gui or not os.environ.get('DISPLAY'):
        cli = True

    # dynamically load GUI elements so we can fall back to the
    # command-line version if we cannot display a GUI
    if cli == True:
        # register a D-Bus handler
        bus.add_signal_receiver(cmdline_event_handler, dbus_interface='com.ubuntu.Upstart0_6',
            signal_name='EventEmitted')
        loop = GLib.MainLoop()
        print('# Upstart Event Monitor (%s)' % _('console mode'))
        print('#')
        print('# %s %s' % (_('Connected to'), destinations[cmdline_args.destination]))
        print('#')
        print('# %s' % _('Columns: time, event and environment'))
        print('')
        loop.run()
    else:
        win = UpstartEventsGui()
        win.connect('delete-event', Gtk.main_quit)
        win.show_all()
        Gtk.main()


if __name__ == '__main__':
    # Allow _() to be called from this point onwards
    gettext.install(NAME)

    main()

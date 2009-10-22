# -*- coding: utf-8 -*-
import sys
import os
import pwd
import pygtk
pygtk.require("2.0")
import gobject
import gtk
import gtk.glade
import CCDBusBackend
from CC_gui_functions import *
from CCDumpList import getDumpList, DumpList
from CCReporterDialog import ReporterDialog
from PluginsSettingsDialog import PluginsSettingsDialog
from SettingsDialog import SettingsDialog
from CCReport import Report
from PluginList import getPluginInfoList
import ABRTExceptions
from abrt_utils import _

try:
    import rpm
except Exception, ex:
    rpm = None


class MainWindow():
    ccdaemon = None
    def __init__(self):
        self.theme = gtk.icon_theme_get_default()
        try:
            self.ccdaemon = CCDBusBackend.DBusManager()
        except ABRTExceptions.IsRunning, e:
            # another instance is running, so exit quietly
            sys.exit()
        except Exception, e:
            # show error message if connection fails
            # FIXME add an option to start the daemon
            gui_error_message("%s" % e)
            sys.exit()
        #Set the Glade file
        self.gladefile = "%s%sccgui.glade" % (sys.path[0],"/")
        self.wTree = gtk.glade.XML(self.gladefile)

        #Get the Main Window, and connect the "destroy" event
        self.window = self.wTree.get_widget("main_window3")
        self.window.set_default_size(700, 480)
        if (self.window):
            self.window.connect("delete_event", self.delete_event_cb)
            self.window.connect("destroy", self.destroy)
            self.window.connect("focus-in-event", self.focus_in_cb)

        self.statusWindow = self.wTree.get_widget("pBarWindow")
        if self.statusWindow:
            self.statusWindow.connect("delete_event", self.sw_delete_event_cb)

        self.appBar = self.wTree.get_widget("appBar")
        # pregress bar window to show while bt is being extracted
        self.pBarWindow = self.wTree.get_widget("pBarWindow")
        self.pBarWindow.set_transient_for(self.window)
        self.pBar = self.wTree.get_widget("pBar")

        # set colours for description heading
        self.wTree.get_widget("evDescription").modify_bg(gtk.STATE_NORMAL, gtk.gdk.color_parse("black"))

        #init the dumps treeview
        self.dlist = self.wTree.get_widget("tvDumps")
        #rows of items with:
        #icon, package_name, application, date, crash_rate, user (only if root), is_reported, ?object?
        if os.getuid() == 0:
            # root
            self.dumpsListStore = gtk.ListStore(gtk.gdk.Pixbuf, str,str,str,str,str,bool, object)
        else:
            self.dumpsListStore = gtk.ListStore(gtk.gdk.Pixbuf, str,str,str,str,bool, object)
        # set filter
        self.modelfilter = self.dumpsListStore.filter_new()
        self.modelfilter.set_visible_func(self.filter_dumps, None)
        self.dlist.set_model(self.modelfilter)
        # add pixbuff separatelly
        icon_column = gtk.TreeViewColumn(_("Icon"))
        icon_column.cell = gtk.CellRendererPixbuf()
        icon_column.cell.set_property('cell-background', "#C9C9C9")
        n = self.dlist.append_column(icon_column)
        icon_column.pack_start(icon_column.cell, False)
        icon_column.set_attributes(icon_column.cell, pixbuf=(n-1), cell_background_set=5+(os.getuid() == 0))
        # ===============================================
        columns = [None]*4
        columns[0] = gtk.TreeViewColumn(_("Package"))
        columns[1] = gtk.TreeViewColumn(_("Application"))
        columns[2] = gtk.TreeViewColumn(_("Date"))
        columns[3] = gtk.TreeViewColumn(_("Crash Rate"))
        if os.getuid() == 0:
            column = gtk.TreeViewColumn(_("User"))
            columns.append(column)
        # create list
        for column in columns:
            n = self.dlist.append_column(column)
            column.cell = gtk.CellRendererText()
            column.pack_start(column.cell, False)
            #column.set_attributes(column.cell, )
            # FIXME: use some relative indexing
            column.cell.set_property('cell-background', "#C9C9C9")
            column.set_attributes(column.cell, text=(n-1), cell_background_set=5+(os.getuid() == 0))
            column.set_resizable(True)
        #connect signals
        self.dlist.connect("cursor-changed", self.on_tvDumps_cursor_changed)
        self.dlist.connect("row-activated", self.on_dumpRowActivated)
        self.dlist.connect("button-press-event", self.on_popupActivate)
        self.wTree.get_widget("bDelete").connect("clicked", self.on_bDelete_clicked, self.dlist)
        self.wTree.get_widget("bReport").connect("clicked", self.on_bReport_clicked)
        self.wTree.get_widget("miQuit").connect("activate", self.on_bQuit_clicked)
        self.wTree.get_widget("miAbout").connect("activate", self.on_miAbout_clicked)
        self.wTree.get_widget("miPlugins").connect("activate", self.on_miPreferences_clicked)
        self.wTree.get_widget("miPreferences").connect("activate", self.on_miSettings_clicked)
        self.wTree.get_widget("miReport").connect("activate", self.on_bReport_clicked)
        self.wTree.get_widget("miDelete").connect("activate", self.on_bDelete_clicked, self.dlist)
        # connect handlers for daemon signals
        self.ccdaemon.connect("crash", self.on_data_changed_cb, None)
        self.ccdaemon.connect("analyze-complete", self.on_analyze_complete_cb, self.pBarWindow)
        self.ccdaemon.connect("abrt-error", self.error_cb)
        #self.ccdaemon.connect("warning", self.warning_cb)
        self.ccdaemon.connect("update", self.update_cb)
        self.ccdaemon.connect("show", self.show_cb)
        self.ccdaemon.connect("daemon-state-changed", self.on_daemon_state_changed_cb)
        self.ccdaemon.connect("report-done", self.on_report_done_cb)

        # load data
        #self.load()
        self.pluginlist = None

    def on_daemon_state_changed_cb(self, widget, state):
        if state == "up":
            self.hydrate() # refresh crash list
            #self.window.set_sensitive(True)
        # abrtd might just die on timeout, it's not fatal
        #elif state == "down":
        #    self.window.set_sensitive(False)

    def on_popupActivate(self, widget, event):
        menu = self.wTree.get_widget("popup_menu")
        # 3 == right mouse button
        if event.button == 3:
            menu.popup(None, None, None, event.button, event.time)

    def on_miAbout_clicked(self, widget):
        dialog = self.wTree.get_widget("about")
        result = dialog.run()
        dialog.hide()

    def on_miPreferences_clicked(self, widget):
        dialog = PluginsSettingsDialog(self.window,self.ccdaemon)
        dialog.hydrate()
        dialog.show()

    def on_miSettings_clicked(self, widget):
        dialog = SettingsDialog(self.window, self.ccdaemon)
        dialog.hydrate()
        dialog.show()
        self.ccdaemon.getSettings()

    def warning_cb(self, daemon, message=None):
        # try to hide the progressbar, we dont really care if it was visible ..
        try:
            #gobject.source_remove(self.timer)
            #self.pBarWindow.hide()
            pass
        except Exception, e:
            pass
        gui_error_message("%s" % message, parent_dialog=self.window)

    def error_cb(self, daemon, message=None):
        # try to hide the progressbar, we dont really care if it was visible ..
        try:
            gobject.source_remove(self.timer)
            self.pBarWindow.hide()
        except Exception, e:
            pass
        gui_error_message(_("Unable to finish current task!\n%s" % message), parent_dialog=self.window)

    def update_cb(self, daemon, message):
        message = message.replace('\n',' ')
        self.wTree.get_widget("lStatus").set_text(message)

    # call to update the progressbar
    def progress_update_cb(self, *args):
        self.pBar.pulse()
        return True

    def hydrate(self):
        n = None
        self.dumpsListStore.clear()
        try:
            dumplist = getDumpList(self.ccdaemon, refresh=True)
        except Exception, e:
            gui_error_message(_("Error while loading the dumplist, please check if abrt daemon is running\n %s" % e))
        for entry in dumplist[::-1]:
            try:
                icon = get_icon_for_package(self.theme, entry.getPackageName())
            except:
                icon = None
            if os.getuid() == 0:
                try:
                    user = pwd.getpwuid(int(entry.getUID()))[0]
                except Exception, e:
                    user = _("Can't get username for uid %s" % entry.getUID())
                n = self.dumpsListStore.append([icon, entry.getPackage(), entry.getExecutable(),
                                                entry.getTime("%c"), entry.getCount(), user, entry.isReported(), entry])
            else:
                n = self.dumpsListStore.append([icon, entry.getPackage(), entry.getExecutable(),
                                                entry.getTime("%c"), entry.getCount(), entry.isReported(), entry])
        # activate the last row if any..
        if n:
            # we can use (0,) as path for the first row, but what if API changes?
            self.dlist.set_cursor(self.dumpsListStore.get_path(self.dumpsListStore.get_iter_first()))

    def filter_dumps(self, model, miter, data):
        # for later..
        return True

    def on_tvDumps_cursor_changed(self,treeview):
        dumpsListStore, path = self.dlist.get_selection().get_selected_rows()
        if not path:
            self.wTree.get_widget("bDelete").set_sensitive(False)
            self.wTree.get_widget("bReport").set_sensitive(False)
            self.wTree.get_widget("lDescription").set_label("")
            return
        self.wTree.get_widget("bDelete").set_sensitive(True)
        self.wTree.get_widget("bReport").set_sensitive(True)
        # this should work until we keep the row object in the last position
        dump = dumpsListStore.get_value(dumpsListStore.get_iter(path[0]), dumpsListStore.get_n_columns()-1)
        #move this to Dump class
        if dump.isReported():
            report_label = _("<b>This crash has been reported, you can find the report(s) at:</b>\n")
            for message in dump.getMessage().split('\n'):
                if message:
                    if "http" in message or "file:///" in message:
                        message = "<a href=\"%s\">%s</a>" % (message, message)
                    report_label += "%s\n" % message
            self.wTree.get_widget("lReported").set_markup(report_label)
        else:
            self.wTree.get_widget("lReported").set_markup(_("<b>Not reported!</b>"))
        lPackage = self.wTree.get_widget("lPackage")
        self.wTree.get_widget("lDescription").set_label(dump.getDescription())

    def on_bDelete_clicked(self, button, treeview):
        dumpsListStore, path = self.dlist.get_selection().get_selected_rows()
        if not path:
            return
        # this should work until we keep the row object in the last position
        dump = dumpsListStore.get_value(dumpsListStore.get_iter(path[0]), dumpsListStore.get_n_columns()-1)
        try:
            if self.ccdaemon.DeleteDebugDump(dump.getUUID()):
                self.hydrate()
                treeview.emit("cursor-changed")
            else:
                print "Couldn't delete"
        except Exception, e:
            print e

    def destroy(self, widget, data=None):
        gtk.main_quit()

    def on_data_changed_cb(self, *args):
        # FIXME mark the new entry somehow....
        # remember the selected row
        dumpsListStore, path = self.dlist.get_selection().get_selected_rows()
        self.hydrate()
        if not path:
            return
        self.dlist.set_cursor(path[0])

    def on_report_done_cb(self, daemon, result):
        try:
            gobject.source_remove(self.timer)
        except:
            pass
        self.pBarWindow.hide()
        gui_report_dialog(result, self.window)
        self.hydrate()

    def on_analyze_complete_cb(self, daemon, report, pBarWindow):
        try:
            gobject.source_remove(self.timer)
        except:
            pass
        self.pBarWindow.hide()
#FIXME - why we need this?? -> timeout warnings
#        try:
#            dumplist = getDumpList(self.ccdaemon)
#        except Exception, e:
#            print e
        if not report:
            gui_error_message(_("Unable to get report!\nDebuginfo is missing?"))
            return
        report_dialog = ReporterDialog(report, self.ccdaemon)
        # (response, report)
        response, result = report_dialog.run()

        if response == gtk.RESPONSE_APPLY:
            try:
                self.update_pBar = False
                self.pBarWindow.show_all()
                self.timer = gobject.timeout_add(100, self.progress_update_cb)
                reporters_settings = {}
                self.pluginlist = getPluginInfoList(self.ccdaemon, refresh=True)
                for plugin in self.pluginlist.getReporterPlugins():
                    reporters_settings[str(plugin)] = plugin.Settings
                self.ccdaemon.Report(result, reporters_settings)
                #self.hydrate()
            except Exception, e:
                gui_error_message(_("Reporting failed!\n%s" % e))
        # -50 == REFRESH
        elif response == -50:
            self.refresh_report(report)

    def refresh_report(self, report):
        self.update_pBar = False
        self.pBarWindow.show_all()
        self.timer = gobject.timeout_add (100,self.progress_update_cb)

        # show the report window with selected report
        try:
            self.ccdaemon.getReport(report["_MWUUID"][2], force=1)
        except Exception, e:
            # FIXME #3	dbus.exceptions.DBusException: org.freedesktop.DBus.Error.NoReply: Did not receive a reply
            # do this async and wait for yum to end with debuginfoinstal
            if self.timer:
                gobject.source_remove(self.timer)
            self.pBarWindow.hide()
            gui_error_message(_("Error getting the report: %s" % e))
        return

    def on_bReport_clicked(self, button):
        dumpsListStore, path = self.dlist.get_selection().get_selected_rows()
        self.on_dumpRowActivated(self.dlist, None, path, None)

    def on_dumpRowActivated(self, treeview, iter, path, user_data=None):
        # FIXME don't duplicate the code, move to function
        dumpsListStore, path = treeview.get_selection().get_selected_rows()
        if not path:
            return
        self.update_pBar = False
        #self.pBar.show()
        self.pBarWindow.show_all()
        self.timer = gobject.timeout_add (100,self.progress_update_cb)

        dump = dumpsListStore.get_value(dumpsListStore.get_iter(path[0]), dumpsListStore.get_n_columns()-1)
        # show the report window with selected dump
        try:
            self.ccdaemon.getReport(dump.getUUID())
        except Exception, e:
            # FIXME #3	dbus.exceptions.DBusException: org.freedesktop.DBus.Error.NoReply: Did not receive a reply
            # do this async and wait for yum to end with debuginfoinstal
            if self.timer:
                gobject.source_remove(self.timer)
            self.pBarWindow.hide()
            gui_error_message(_("Error getting the report: %s" % e))
        return

    def sw_delete_event_cb(self, widget, event, data=None):
        if self.timer:
            gobject.source_remove(self.timer)
        widget.hide()
        return True

    def delete_event_cb(self, widget, event, data=None):
        gtk.main_quit()

    def focus_in_cb(self, widget, event, data=None):
        self.window.set_urgency_hint(False)

    def on_bQuit_clicked(self, widget):
        gtk.main_quit()

    def show(self):
        self.window.show()

    def show_cb(self, daemon):
        if self.window:
            if self.window.is_active():
                return
            self.window.set_urgency_hint(True)
            self.window.present()

if __name__ == "__main__":
    cc = MainWindow()
    cc.hydrate()
    cc.show()
    gtk.main()


from dbus.mainloop.glib import DBusGMainLoop
import dbus, gobject, re, sys, time, subprocess, os, json
import zmq

def purple_recv(tx, account, sender, message, conv, flags):
    print("maybe got one?",account,sender,message,conv,flags,tx)
    
    print(account)
    data = {"message":message, "sender":sender, "conv":conv, "account":account}
    tx.send_string(json.dumps(data))
    print("sender: {} message: {}, account: {}, conversation: {}, flags: {}, conv: {}".format(sender,message,account,conv,flags,conv))
    
print("STARTING THREAD")
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
ctx = zmq.Context()
tx = ctx.socket(zmq.DEALER)
tx.connect("ipc:///tmp/purple.ipc")

def _recv(account, sender, message, conv, flags):
    purple_recv(tx, account, sender, message, conv, flags)
    
bus.add_signal_receiver(_recv, dbus_interface="im.pidgin.purple.PurpleInterface", signal_name="ReceivedImMsg")
loop = gobject.MainLoop()
loop.run()

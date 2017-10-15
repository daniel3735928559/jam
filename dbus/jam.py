from IPython import embed

import dbus, gobject, re, sys, time, subprocess, os
import zmq
from libmango import *

class jam(m_node):
    def __init__(self):
        super().__init__(debug=True)
        self.interface.add_interface('jam.yaml',{'im_send':self.send})

        # Setup socket for receiving messages
        self.purple_sock = self.context.socket(zmq.ROUTER)
        self.purple_sock.bind("ipc:///tmp/purple.ipc")
        self.add_socket(self.purple_sock, self.purple_recv, self.purple_err)
        self.purple_process = subprocess.Popen(["python", "purple.py"], cwd=os.path.dirname(os.path.realpath(__file__)), env=os.environ)
        
        # Get DBus instance for sending messages
        bus = dbus.SessionBus()
        obj = bus.get_object("im.pidgin.purple.PurpleService", "/im/pidgin/purple/PurpleObject")
        self.purple = dbus.Interface(obj, "im.pidgin.purple.PurpleInterface")

        self.run()

    def purple_recv(self):
        rt,msg = self.purple_sock.recv_multipart()
        msg= json.loads(msg)
        self.m_send("im_recv",msg)
        
    def purple_err(self):
        self.debug_print("PRPL DIED")
                        
    def send(self, header, args):
        self.debug_print("SENDING",header,args)
        target = self.purple.PurpleConvIm(int(args['conv']))
        self.purple.PurpleConvImSend(target, args['msg'])

        
jam()

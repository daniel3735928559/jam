from IPython import embed

import dbus, gobject, re, sys, time, subprocess, os
import zmq
from libmango import *
from fuzzywuzzy import process

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

        self.accounts = self.purple.PurpleAccountsGetAll()
        self.buddies = {}
        for acc in self.accounts:
            bids = self.purple.PurpleFindBuddies(acc, "")
            
            for bid in bids:
                buddy = {
                    "id": bid,
                    "name": self.purple.PurpleBuddyGetName(bid),
                    "alias": self.purple.PurpleBuddyGetAlias(bid),
                    "account_id": acc
                }
                self.buddies[buddy["alias"]] = buddy
        self.match_quality_threshold = 80
        self.run()

    def purple_recv(self):
        rt,msg = self.purple_sock.recv_multipart()
        msg= json.loads(msg)
        self.m_send("im_recv",msg)
        
    def purple_err(self):
        self.debug_print("PRPL DIED")
                        
    def send(self, header, args):
        target,quality = process.extractOne(args['to'],self.buddies.keys())
        if quality < self.match_quality_threshold:
            self.debug_print("target={} not close enough to best match={}".format(target,args['to']))
            return
        buddy = self.buddies[target]
        conv = self.purple.PurpleConversationNew(1, int(buddy['account_id']), str(buddy['name']))
        im = self.purple.PurpleConvIm(conv)
        self.debug_print("SENDING",header,args,target,buddy,conv,im)
        self.purple.PurpleConvImSend(im, args['msg'])

        
jam()

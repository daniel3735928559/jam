from IPython import embed

import dbus, gobject, re, sys, time, subprocess, os
import zmq
from libmango import *
from fuzzywuzzy import process

class jam(m_node):
    def __init__(self):
        super().__init__(debug=True)
        self.interface.add_interface('jam.yaml',{'im_send':self.send,'im_send_to':self.send_to})

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
        self.buddies = []
        for acc in self.accounts:
            bids = self.purple.PurpleFindBuddies(acc, "")
            
            for bid in bids:
                buddy = {
                    "id": bid,
                    "name": self.purple.PurpleBuddyGetName(bid),
                    "alias": self.purple.PurpleBuddyGetAlias(bid),
                    "account_id": acc
                }
                self.buddies.append(buddy)
        self.match_quality_threshold = 95
        self.run()

    def purple_recv(self):
        rt,msg = self.purple_sock.recv_multipart()
        msg= json.loads(msg)
        self.m_send("im_recv",msg)
        
    def purple_err(self):
        self.debug_print("PRPL DIED")
                        
    def send(self, header, args):
        candidates = process.extract({"name":args['to']},self.buddies,processor=lambda b: b['name'],limit=3)
        candidates += process.extract({"alias":args['to']},self.buddies,processor=lambda b: b['alias'],limit=3)
        candidates = sorted([c for c in candidates if c[1] > self.match_quality_threshold],key=lambda c: c[1])
        self.debug_print("Filtered candidates:",candidates)
        if len(candidates) == 1 or (len(candidates) == 2 and candidates[0][0] == candidates[1][0]):
            buddy = candidates[0][0]
        elif len(candidates) == 0:
            self.debug_print("Target not found: {}".format(args['to']))
            return
        else:
            self.debug_print("Ambiguous target: {}\nSome possible matches:\n{}".format(args['to'],"\n".join(["- Id: {}, Alias: {}, Quality: {}".format(c[0]['name'], c[0]['alias'], c[1]) for c in candidates])))
            return
        
        conv = self.purple.PurpleConversationNew(1, int(buddy['account_id']), str(buddy['name']))
        im = self.purple.PurpleConvIm(conv)
        self.debug_print("SENDING",header,args,buddy,conv,im)
        self.purple.PurpleConvImSend(im, args['msg'])

    def send_to(self, header, args):
        im = self.purple.PurpleConvIm(int(args['conv']))
        self.debug_print("SENDING",header,args,im)
        self.purple.PurpleConvImSend(im, args['msg'])

        
jam()

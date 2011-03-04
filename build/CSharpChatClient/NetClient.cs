using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace CSharpChatClient
{
    class NetClient : EasySphynxClient
    {
        public NetClient()
            : base()
        {

        }

        public override void OnDisconnect(string reason)
        {
            System.Windows.Forms.MessageBox.Show("disconnected: " + reason);
        }

        public override void OnConnectFailure(string reason)
        {
            System.Windows.Forms.MessageBox.Show("failure: " + reason);
        }

        public override void OnConnectSuccess()
        {
            System.Windows.Forms.MessageBox.Show("conn");
        }

        public override unsafe void OnMessageArrivals(IntPtr msgs, int count)
        {
            Sphynx.IncomingMessage []messages = Sphynx.GetMessages(msgs, count);

            System.Windows.Forms.MessageBox.Show("msg");
        }
    }
}

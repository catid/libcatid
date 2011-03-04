using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows.Forms;

namespace CSharpChatClient
{
    static class Program
    {
        static void DoMain()
        {
            NetClient nc = new NetClient();

            byte[] public_key = System.IO.File.ReadAllBytes("PublicKey.bin");

            nc.Connect("127.0.0.1", 22000, public_key, public_key.Length, "Chat");

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new ChatForm());
        }

        /// <summary>
        /// The main entry point for the application.
        /// </summary>
        [STAThread]
        static void Main()
        {
            Sphynx.AssemblyResolveHook();

            DoMain();
        }
    }
}

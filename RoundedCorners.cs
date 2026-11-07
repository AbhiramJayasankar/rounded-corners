using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using Microsoft.Win32;

namespace RoundedCorners
{
    static class Native
    {
        [DllImport("user32.dll")]
        public static extern bool SetProcessDPIAware();

        [DllImport("user32.dll", SetLastError = true)]
        public static extern bool UpdateLayeredWindow(IntPtr hwnd, IntPtr hdcDst, ref POINT pptDst,
            ref SIZE psize, IntPtr hdcSrc, ref POINT pptSrc, int crKey, ref BLENDFUNCTION pblend, int dwFlags);

        [DllImport("user32.dll")]
        public static extern IntPtr GetDC(IntPtr hWnd);

        [DllImport("user32.dll")]
        public static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);

        [DllImport("gdi32.dll")]
        public static extern IntPtr CreateCompatibleDC(IntPtr hDC);

        [DllImport("gdi32.dll")]
        public static extern bool DeleteDC(IntPtr hdc);

        [DllImport("gdi32.dll")]
        public static extern IntPtr SelectObject(IntPtr hDC, IntPtr hObject);

        [DllImport("gdi32.dll")]
        public static extern bool DeleteObject(IntPtr hObject);

        [DllImport("user32.dll")]
        public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndAfter, int x, int y, int cx, int cy, uint flags);

        public static readonly IntPtr HWND_TOPMOST = new IntPtr(-1);
        public const uint SWP_NOMOVE = 0x0002;
        public const uint SWP_NOSIZE = 0x0001;
        public const uint SWP_NOACTIVATE = 0x0010;
        public const uint SWP_SHOWWINDOW = 0x0040;

        public const int ULW_ALPHA = 0x02;
        public const byte AC_SRC_OVER = 0x00;
        public const byte AC_SRC_ALPHA = 0x01;

        [StructLayout(LayoutKind.Sequential)]
        public struct POINT { public int x; public int y; }

        [StructLayout(LayoutKind.Sequential)]
        public struct SIZE { public int cx; public int cy; }

        [StructLayout(LayoutKind.Sequential)]
        public struct BLENDFUNCTION
        {
            public byte BlendOp;
            public byte BlendFlags;
            public byte SourceConstantAlpha;
            public byte AlphaFormat;
        }
    }

    // One small layered window per screen corner. Click-through, topmost, never activated.
    class CornerWindow : Form
    {
        readonly Screen screen;
        readonly int corner; // 0=TL 1=TR 2=BL 3=BR

        public CornerWindow(Screen s, int c)
        {
            screen = s;
            corner = c;
            FormBorderStyle = FormBorderStyle.None;
            ShowInTaskbar = false;
            StartPosition = FormStartPosition.Manual;
            TopMost = true;
        }

        protected override CreateParams CreateParams
        {
            get
            {
                CreateParams cp = base.CreateParams;
                // WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE
                cp.ExStyle |= 0x80000 | 0x20 | 0x80 | 0x8000000;
                return cp;
            }
        }

        protected override bool ShowWithoutActivation { get { return true; } }

        public void Apply(int radius)
        {
            if (radius < 1) { Hide(); return; }

            Rectangle b = screen.Bounds;
            int x = (corner == 0 || corner == 2) ? b.Left : b.Right - radius;
            int y = (corner == 0 || corner == 1) ? b.Top : b.Bottom - radius;

            // Render one canonical top-left corner and mirror it, so all four
            // corners are pixel-identical by construction.
            using (Bitmap bmp = RenderTopLeft(radius))
            {
                if (corner == 1) bmp.RotateFlip(RotateFlipType.RotateNoneFlipX);
                else if (corner == 2) bmp.RotateFlip(RotateFlipType.RotateNoneFlipY);
                else if (corner == 3) bmp.RotateFlip(RotateFlipType.RotateNoneFlipXY);
                PushBitmap(bmp, new Point(x, y));
            }

            if (!Visible) Show();
            Reassert();
        }

        // Black fill drawn as an antialiased path on a transparent bitmap.
        // (Punching a hole with CompositingMode.SourceCopy disables antialiasing
        // and produces jagged, asymmetric edges.)
        static Bitmap RenderTopLeft(int r)
        {
            Bitmap bmp = new Bitmap(r, r, PixelFormat.Format32bppArgb);
            using (Graphics g = Graphics.FromImage(bmp))
            using (GraphicsPath p = new GraphicsPath())
            {
                g.SmoothingMode = SmoothingMode.AntiAlias;
                // Half = area-coverage fill convention; without it (and the 2px
                // overdraw past the bitmap edge) the outermost row/column at the
                // screen edge renders only half-opaque and the background bleeds
                // through as a thin line.
                g.PixelOffsetMode = PixelOffsetMode.Half;
                p.AddLine(-2, -2, r, -2);
                p.AddLine(r, -2, r, 0);
                p.AddArc(0f, 0f, 2f * r, 2f * r, 270f, -90f);
                p.AddLine(0, r, -2, r);
                p.CloseFigure();
                g.FillPath(Brushes.Black, p);
            }
            return bmp;
        }

        public void Reassert()
        {
            if (IsHandleCreated && Visible)
                Native.SetWindowPos(Handle, Native.HWND_TOPMOST, 0, 0, 0, 0,
                    Native.SWP_NOMOVE | Native.SWP_NOSIZE | Native.SWP_NOACTIVATE);
        }

        void PushBitmap(Bitmap bmp, Point pos)
        {
            IntPtr screenDc = Native.GetDC(IntPtr.Zero);
            IntPtr memDc = Native.CreateCompatibleDC(screenDc);
            IntPtr hBitmap = IntPtr.Zero;
            IntPtr oldBitmap = IntPtr.Zero;
            try
            {
                hBitmap = bmp.GetHbitmap(Color.FromArgb(0));
                oldBitmap = Native.SelectObject(memDc, hBitmap);

                Native.SIZE size = new Native.SIZE { cx = bmp.Width, cy = bmp.Height };
                Native.POINT src = new Native.POINT { x = 0, y = 0 };
                Native.POINT topPos = new Native.POINT { x = pos.X, y = pos.Y };
                Native.BLENDFUNCTION blend = new Native.BLENDFUNCTION
                {
                    BlendOp = Native.AC_SRC_OVER,
                    BlendFlags = 0,
                    SourceConstantAlpha = 255,
                    AlphaFormat = Native.AC_SRC_ALPHA
                };
                Native.UpdateLayeredWindow(Handle, screenDc, ref topPos, ref size,
                    memDc, ref src, 0, ref blend, Native.ULW_ALPHA);
            }
            finally
            {
                Native.ReleaseDC(IntPtr.Zero, screenDc);
                if (hBitmap != IntPtr.Zero)
                {
                    Native.SelectObject(memDc, oldBitmap);
                    Native.DeleteObject(hBitmap);
                }
                Native.DeleteDC(memDc);
            }
        }
    }

    class SettingsForm : Form
    {
        readonly List<CornerWindow> corners = new List<CornerWindow>();
        readonly TrackBar bar;
        readonly Label lbl;
        readonly NotifyIcon tray;
        readonly Timer topmostTimer;
        static readonly string CfgPath =
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "RoundedCorners.cfg");

        public SettingsForm()
        {
            Text = "Rounded Corners";
            FormBorderStyle = FormBorderStyle.FixedSingle;
            MaximizeBox = false;
            ClientSize = new Size(340, 110);
            StartPosition = FormStartPosition.CenterScreen;

            lbl = new Label { Left = 10, Top = 10, Width = 320 };
            bar = new TrackBar { Left = 10, Top = 32, Width = 320, Minimum = 0, Maximum = 120, TickFrequency = 10 };
            bar.ValueChanged += delegate { ApplyRadius(); };

            Button hideBtn = new Button { Left = 10, Top = 78, Width = 155, Text = "Hide to tray" };
            hideBtn.Click += delegate { Hide(); };
            Button exitBtn = new Button { Left = 175, Top = 78, Width = 155, Text = "Exit" };
            exitBtn.Click += delegate { QuitApp(); };

            Controls.Add(lbl);
            Controls.Add(bar);
            Controls.Add(hideBtn);
            Controls.Add(exitBtn);

            ContextMenuStrip menu = new ContextMenuStrip();
            menu.Items.Add("Settings", null, delegate { ShowSettings(); });
            menu.Items.Add("Exit", null, delegate { QuitApp(); });
            tray = new NotifyIcon
            {
                Icon = SystemIcons.Application,
                Text = "Rounded Corners",
                Visible = true,
                ContextMenuStrip = menu
            };
            tray.DoubleClick += delegate { ShowSettings(); };

            // Fast reassert so the corners hop back above the auto-hide taskbar
            // (also a topmost window) as soon as it slides in.
            topmostTimer = new Timer { Interval = 250 };
            topmostTimer.Tick += delegate { foreach (CornerWindow c in corners) c.Reassert(); };
            topmostTimer.Start();

            SystemEvents.DisplaySettingsChanged += OnDisplayChanged;

            bar.Value = LoadRadius();
            BuildCorners();
            ApplyRadius();
        }

        void OnDisplayChanged(object sender, EventArgs e)
        {
            BuildCorners();
            ApplyRadius();
        }

        void BuildCorners()
        {
            foreach (CornerWindow c in corners) c.Close();
            corners.Clear();
            foreach (Screen s in Screen.AllScreens)
                for (int i = 0; i < 4; i++)
                    corners.Add(new CornerWindow(s, i));
        }

        void ApplyRadius()
        {
            int r = bar.Value;
            lbl.Text = "Corner radius: " + r + " px";
            foreach (CornerWindow c in corners) c.Apply(r);
            SaveRadius(r);
        }

        void ShowSettings()
        {
            Show();
            WindowState = FormWindowState.Normal;
            Activate();
        }

        void QuitApp()
        {
            tray.Visible = false;
            SystemEvents.DisplaySettingsChanged -= OnDisplayChanged;
            Application.Exit();
        }

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            // Closing the settings window just hides it; the overlay keeps running in the tray.
            if (e.CloseReason == CloseReason.UserClosing)
            {
                e.Cancel = true;
                Hide();
                return;
            }
            base.OnFormClosing(e);
        }

        static int LoadRadius()
        {
            try
            {
                int v;
                if (File.Exists(CfgPath) && int.TryParse(File.ReadAllText(CfgPath).Trim(), out v))
                    return Math.Max(0, Math.Min(120, v));
            }
            catch { }
            return 24;
        }

        static void SaveRadius(int r)
        {
            try { File.WriteAllText(CfgPath, r.ToString()); } catch { }
        }
    }

    static class Program
    {
        [STAThread]
        static void Main()
        {
            Native.SetProcessDPIAware();
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new SettingsForm());
        }
    }
}

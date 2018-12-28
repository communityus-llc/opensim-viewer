﻿using System;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Xenko.Core;
using Xenko.Core.Diagnostics;
using Xenko.Core.Mathematics;
using Xenko.Engine;
using Xenko.Input;
using Xenko.Physics;
using Xenko.UI.Controls;
using HtmlAgilityPack;
using PotamOS.Interfaces;

namespace PotamOS
{
    public class SceneControl : SyncScript, IEngine
    {
        private Task<Scene> loadingTask;

        /// <summary>
        /// The loaded scene
        /// </summary>
        [DataMemberIgnore]
        public Scene Instance { get; private set; }

        enum State {
            WaitingForAddress = 0,
            WaitingToEnter = 1,
            DynamicScene = 2
        };
        
        private State m_State = State.WaitingForAddress;
        
        [DefaultValue(false)]
        public bool ChangeScene { get; set; } = false;

        /// <summary>
        /// The url of the scene to load
        /// </summary>
        public string Url { get; set; }
        
        ///
        /// The url of the server-side currently connected to
        ///
        public string HppoStr { get; set; }

        public String Name
        {
            get { return "Xenko"; }
        }

        /// <summary>
        /// The cordinate system of this engine wrt OpenSim
        /// TODO: FIXME
        /// </summary>
        public Vector3 CoordinateSystem { get; } = new Vector3();

        private HtmlDocument m_CurrentHtmlDoc = null;

        private IController controller;
        public IController Controller
        {
            get { return controller; }
        }

        public override void Start()
        {
            base.Start();

            // Initialization of the script.
            var fileWriter = new TextWriterLogListener(new FileStream("Potamos.log", FileMode.Create));
            GlobalLogger.GlobalMessageLogged += fileWriter;

            Log.Info("Starting");

            // Start the controller
            controller = new ControllerAsync(this);

            Url = "SplashScene";
            LoadScene();
        }            

        public override void Update()
        {
            if (Url == "DynamicScene" && Input.DownKeys.Any(e => e.Equals(Keys.Escape)))
            {
                NewPage(UIPages.Splash, "");
            }
        }

        public void Goto(string hppo)
        {
            HppoStr = hppo;
            Controller.Goto(HppoStr);
        }
        
        public void SubmitForm(Scene hs)
        {
            Log.Info("SubmitForm");

            Entity ui = hs.Entities.First(e => e.Name == "UI");
            UIComponent uic = ui.Get<UIComponent>();
            EditText nameBox = (EditText)uic.Page.RootElement.FindName("Name");
            Log.Info("Entered name is " + nameBox.Text);

            foreach (var n in m_CurrentHtmlDoc.DocumentNode.SelectNodes("//form/input"))
                Log.Info(n.Id + ":" + n.Name + ":" + string.Join<HtmlAttribute>(",", n.Attributes));

            HtmlNode nameNode = m_CurrentHtmlDoc.DocumentNode.SelectNodes("//form/input").First(n => n.GetAttributeValue("name", string.Empty) == "NAME");
            nameNode.SetAttributeValue("value", nameBox.Text);

            Dictionary<string, string> data = new Dictionary<string, string>();
            foreach (var n in m_CurrentHtmlDoc.DocumentNode.SelectNodes("//form/input"))
                if (n.HasAttributes) {
                    string name  = n.GetAttributeValue("name", string.Empty);
                    string value = n.GetAttributeValue("value", string.Empty);
                    if (!string.IsNullOrEmpty(name))
                    {
                        Log.Info(name + " - " + value);
                        data.Add(name, value);
                    }
                }

            Controller.SubmitForm(data);
        }

        private void LoadScene()
        {
            if (Instance != null)
            {
                Content.Unload(Instance);

                if (!Content.IsLoaded(Url))
                {
                    Instance.Parent = null;
                }

                Instance = null;
            }

            Log.Info("Loading scene " + Url);
            Instance = Content.Load<Scene>(Url);
            loadingTask = Task.FromResult(Instance);
            // Once loaded, add it to the scene
            if (Instance != null)
                Instance.Parent = Entity.Scene;

        }

        public void NewPage(UIPages page, string html)
        {
            Log.Info("New page " + page);

            switch (page)
            {
                case (UIPages.Handshake):
                    Url = "HandshakeScene";
                    break;
                case (UIPages.DynamicScene):
                    Url = "DynamicScene";
                    break;
                case (UIPages.Splash):
                default:
                    Url = "SplashScene";
                    break;
            }
            // Synchronous
            LoadScene();

            if (page == UIPages.Handshake)
            {
                m_CurrentHtmlDoc = new HtmlDocument();
                m_CurrentHtmlDoc.LoadHtml(html);
                Log.Info(html);
                string regionName = "DEFAULT";
                foreach (var i in m_CurrentHtmlDoc.DocumentNode.SelectNodes("//form/input"))
                {
                    if (i.HasAttributes && i.GetAttributeValue("name", string.Empty) == "REGION")
                    {
                        regionName = i.GetAttributeValue("value", regionName);
                        Log.Info("Region is " + regionName);
                    }
                }
                // This doesn't update the text on the screen. I'm missing something...
                Entity ui = Entity.Scene.Entities.First(e => e.Name == "UI");
                UIComponent uic = ui.Get<UIComponent>();
                TextBlock regionBlock = (TextBlock)uic.Page.RootElement.FindName("RegionBlock");
                regionBlock.Text = regionBlock.Text + " " + regionName;
            }
        }
    }
}

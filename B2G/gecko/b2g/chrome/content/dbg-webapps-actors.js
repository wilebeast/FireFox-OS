/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

let Cu = Components.utils;
let Cc = Components.classes;
let Ci = Components.interfaces;

function debug(aMsg) {
  /*
  Cc["@mozilla.org/consoleservice;1"]
    .getService(Ci.nsIConsoleService)
    .logStringMessage("--*-- WebappsActor : " + aMsg);
  */
}

#ifdef MOZ_WIDGET_GONK
  const DIRECTORY_NAME = "webappsDir";
#else
  const DIRECTORY_NAME = "ProfD";
#endif

/**
 * Creates a WebappsActor. WebappsActor provides remote access to
 * install apps.
 */
function WebappsActor(aConnection) { debug("init"); }

WebappsActor.prototype = {
  actorPrefix: "webapps",

  _registerApp: function wa_actorRegisterApp(aApp, aId, aDir) {
    let reg = DOMApplicationRegistry;
    let self = this;

    aApp.installTime = Date.now();
    aApp.installState = "installed";
    aApp.removable = true;
    aApp.id = aId;
    aApp.basePath = FileUtils.getDir(DIRECTORY_NAME, ["webapps"], true).path;
    aApp.localId = (aId in reg.webapps) ? reg.webapps[aId].localId
                                        : reg._nextLocalId();

    reg.webapps[aId] = aApp;
    reg.updatePermissionsForApp(aId);

    reg._readManifests([{ id: aId }], function(aResult) {
      let manifest = aResult[0].manifest;
      aApp.name = manifest.name;
      reg.updateAppHandlers(null, manifest, aApp);
      reg._saveApps(function() {
        aApp.manifest = manifest;
        reg.broadcastMessage("Webapps:AddApp", { id: aId, app: aApp });
        reg.broadcastMessage("Webapps:Install:Return:OK",
                             { app: aApp,
                               oid: "foo",
                               requestID: "bar"
                             });
        delete aApp.manifest;
        self.conn.send({ from: self.actorID,
                         type: "webappsEvent",
                         appId: aId
                       });

        // We can't have appcache for packaged apps.
        if (!aApp.origin.startsWith("app://")) {
          reg.startOfflineCacheDownload(new ManifestHelper(manifest, aApp.origin));
        }
      });
      // Cleanup by removing the temporary directory.
      aDir.remove(true);
    });
  },

  _sendError: function wa_actorSendError(aMsg, aId) {
    debug("Sending error: " + aMsg);
    this.conn.send(
      { from: this.actorID,
        type: "webappsEvent",
        appId: aId,
        error: "installationFailed",
        message: aMsg
      });
  },

  _getAppType: function wa_actorGetAppType(aType) {
    let type = Ci.nsIPrincipal.APP_STATUS_INSTALLED;

    if (aType) {
      type = aType == "privileged" ? Ci.nsIPrincipal.APP_STATUS_PRIVILEGED
           : aType == "certified" ? Ci.nsIPrincipal.APP_STATUS_CERTIFIED
           : Ci.nsIPrincipal.APP_STATUS_INSTALLED;
    }

    return type;
  },

  installHostedApp: function wa_actorInstallHosted(aDir, aId) {
    debug("installHostedApp");
    let self = this;

    let runnable = {
      run: function run() {
        try {
          // Move manifest.webapp to the destination directory.
          let manFile = aDir.clone();
          manFile.append("manifest.webapp");
          DOMApplicationRegistry._loadJSONAsync(manFile, function(aManifest) {
            if (!aManifest) {
              self._sendError("Error Parsing manifest.webapp", aId);
              return;
            }

            let appType = self._getAppType(aManifest.type);

            // In production builds, don't allow installation of certified apps.
#ifdef MOZ_OFFICIAL_BRANDING
            if (appType == Ci.nsIPrincipal.APP_STATUS_CERTIFIED) {
              self._sendError("Installing certified apps is not allowed.", aId);
              return;
            }
#endif
            // The destination directory for this app.
            let installDir = FileUtils.getDir(DIRECTORY_NAME,
                                              ["webapps", aId], true);
            manFile.moveTo(installDir, "manifest.webapp");

            // Read the origin and manifest url from metadata.json
            let metaFile = aDir.clone();
            metaFile.append("metadata.json");
            DOMApplicationRegistry._loadJSONAsync(metaFile, function(aMetadata) {
              if (!aMetadata) {
                self._sendError("Error Parsing metadata.json", aId);
                return;
              }

              if (!aMetadata.origin) {
                self._sendError("Missing 'origin' property in metadata.json", aId);
                return;
              }

              let origin = aMetadata.origin;
              let manifestURL = aMetadata.manifestURL ||
                                origin + "/manifest.webapp";
              // Create a fake app object with the minimum set of properties we need.
              let app = {
                origin: origin,
                installOrigin: aMetadata.installOrigin || origin,
                manifestURL: manifestURL,
                appStatus: appType
              };

              self._registerApp(app, aId, aDir);
            });
          });
        } catch(e) {
          // If anything goes wrong, just send it back.
          self._sendError(e.toString(), aId);
        }
      }
    }

    Services.tm.currentThread.dispatch(runnable,
                                       Ci.nsIThread.DISPATCH_NORMAL);
  },

  installPackagedApp: function wa_actorInstallPackaged(aDir, aId) {
    debug("installPackagedApp");
    let self = this;

    let runnable = {
      run: function run() {
        try {
          // The destination directory for this app.
          let installDir = FileUtils.getDir(DIRECTORY_NAME,
                                            ["webapps", aId], true);

          // Move application.zip to the destination directory, and
          // extract manifest.webapp there.
          let zipFile = aDir.clone();
          zipFile.append("application.zip");
          let zipReader = Cc["@mozilla.org/libjar/zip-reader;1"]
                            .createInstance(Ci.nsIZipReader);
          zipReader.open(zipFile);
          let manFile = installDir.clone();
          manFile.append("manifest.webapp");
          zipReader.extract("manifest.webapp", manFile);
          zipReader.close();
          zipFile.moveTo(installDir, "application.zip");

          DOMApplicationRegistry._loadJSONAsync(manFile, function(aManifest) {
            if (!aManifest) {
              self._sendError("Error Parsing manifest.webapp", aId);
            }

            let appType = self._getAppType(aManifest.type);

            // In production builds, don't allow installation of certified apps.
#ifdef MOZ_OFFICIAL_BRANDING
            if (appType == Ci.nsIPrincipal.APP_STATUS_CERTIFIED) {
              self._sendError("Installing certified apps is not allowed.", aId);
              return;
            }
#endif
            let origin = "app://" + aId;

            // Create a fake app object with the minimum set of properties we need.
            let app = {
              origin: origin,
              installOrigin: origin,
              manifestURL: origin + "/manifest.webapp",
              appStatus: appType
            }

            self._registerApp(app, aId, aDir);
          });
        } catch(e) {
          // If anything goes wrong, just send it back.
          self._sendError(e.toString(), aId);
        }
      }
    }

    Services.tm.currentThread.dispatch(runnable,
                                       Ci.nsIThread.DISPATCH_NORMAL);
  },

  /**
    * @param appId   : The id of the app we want to install. We will look for
    *                  the files for the app in $TMP/b2g/$appId :
    *                  For packaged apps: application.zip
    *                  For hosted apps:   metadata.json and manifest.webapp
    */
  install: function wa_actorInstall(aRequest) {
    debug("install");

    Cu.import("resource://gre/modules/Webapps.jsm");
    Cu.import("resource://gre/modules/AppsUtils.jsm");
    Cu.import("resource://gre/modules/FileUtils.jsm");

    let appId = aRequest.appId;
    if (!appId) {
      return { error: "missingParameter",
               message: "missing parameter appId" }
    }

    // Check that we are not overriding a preinstalled application.
    let reg = DOMApplicationRegistry;
    if (appId in reg.webapps && reg.webapps[appId].removable === false) {
      return { error: "badParameterType",
               message: "The application " + appId + " can't be overriden."
             }
    }

    let appDir = FileUtils.getDir("TmpD", ["b2g", appId], false, false);

    if (!appDir || !appDir.exists()) {
      return { error: "badParameterType",
               message: "missing directory " + appDir.path
             }
    }

    let testFile = appDir.clone();
    testFile.append("application.zip");

    if (testFile.exists()) {
      this.installPackagedApp(appDir, appId);
    } else {
      let missing =
        ["manifest.webapp", "metadata.json"]
        .some(function(aName) {
          testFile = appDir.clone();
          testFile.append(aName);
          return !testFile.exists();
        });

      if (missing) {
        try {
          appDir.remove(true);
        } catch(e) {}
        return { error: "badParameterType",
                 message: "hosted app file is missing" }
      }

      this.installHostedApp(appDir, appId);
    }

    return { appId: appId, path: appDir.path }
  }
};

/**
 * The request types this actor can handle.
 */
WebappsActor.prototype.requestTypes = {
  "install": WebappsActor.prototype.install
};

DebuggerServer.addGlobalActor(WebappsActor, "webappsActor");

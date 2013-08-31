/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const DEBUG = false;
function debug(s) { dump("-*- ContactManager: " + s + "\n"); }

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/DOMRequestHelper.jsm");

XPCOMUtils.defineLazyGetter(Services, "DOMRequest", function() {
  return Cc["@mozilla.org/dom/dom-request-service;1"].getService(Ci.nsIDOMRequestService);
});

XPCOMUtils.defineLazyServiceGetter(this, "pm",
                                   "@mozilla.org/permissionmanager;1",
                                   "nsIPermissionManager");

XPCOMUtils.defineLazyServiceGetter(this, "cpmm",
                                   "@mozilla.org/childprocessmessagemanager;1",
                                   "nsIMessageSender");

function stringOrBust(aObj) {
  if (typeof aObj != "string") {
    if (DEBUG) debug("Field is not a string and was ignored.");
    return undefined;
  } else {
    return aObj;
  }
}

function sanitizeStringArray(aArray) {
  if (!Array.isArray(aArray)) {
    aArray = [aArray];
  }
  return aArray.map(stringOrBust).filter(function(el) { return el != undefined; });
}

const CONTACTS_SENDMORE_MINIMUM = 5;

const nsIClassInfo            = Ci.nsIClassInfo;
const CONTACTPROPERTIES_CID   = Components.ID("{86504745-da3e-4a17-b67b-88b40e0e6bc8}");
const nsIDOMContactProperties = Ci.nsIDOMContactProperties;

// ContactProperties is not directly instantiated. It is used as interface.

function ContactProperties(aProp) { if (DEBUG) debug("ContactProperties Constructor"); }

ContactProperties.prototype = {

  classID : CONTACTPROPERTIES_CID,
  classInfo : XPCOMUtils.generateCI({classID: CONTACTPROPERTIES_CID,
                                     contractID:"@mozilla.org/contactProperties;1",
                                     classDescription: "ContactProperties",
                                     interfaces: [nsIDOMContactProperties],
                                     flags: nsIClassInfo.DOM_OBJECT}),

  QueryInterface : XPCOMUtils.generateQI([nsIDOMContactProperties])
}

//ContactAddress

const CONTACTADDRESS_CONTRACTID = "@mozilla.org/contactAddress;1";
const CONTACTADDRESS_CID        = Components.ID("{eba48030-89e8-11e1-b0c4-0800200c9a66}");
const nsIDOMContactAddress      = Components.interfaces.nsIDOMContactAddress;

function ContactAddress(aType, aStreetAddress, aLocality, aRegion, aPostalCode, aCountryName) {
  this.type = sanitizeStringArray(aType);
  this.streetAddress = stringOrBust(aStreetAddress);
  this.locality = stringOrBust(aLocality);
  this.region = stringOrBust(aRegion);
  this.postalCode = stringOrBust(aPostalCode);
  this.countryName = stringOrBust(aCountryName);
};

ContactAddress.prototype = {
  __exposedProps__: {
                      type: 'rw',
                      streetAddress: 'rw',
                      locality: 'rw',
                      region: 'rw',
                      postalCode: 'rw',
                      countryName: 'rw'
                     },

  classID : CONTACTADDRESS_CID,
  classInfo : XPCOMUtils.generateCI({classID: CONTACTADDRESS_CID,
                                     contractID: CONTACTADDRESS_CONTRACTID,
                                     classDescription: "ContactAddress",
                                     interfaces: [nsIDOMContactAddress],
                                     flags: nsIClassInfo.DOM_OBJECT}),

  QueryInterface : XPCOMUtils.generateQI([nsIDOMContactAddress])
}

//ContactField

const CONTACTFIELD_CONTRACTID = "@mozilla.org/contactField;1";
const CONTACTFIELD_CID        = Components.ID("{e2cb19c0-e4aa-11e1-9b23-0800200c9a66}");
const nsIDOMContactField      = Components.interfaces.nsIDOMContactField;

function ContactField(aType, aValue) {
  this.type = sanitizeStringArray(aType);
  this.value = stringOrBust(aValue);
};

ContactField.prototype = {
  __exposedProps__: {
                      type: 'rw',
                      value: 'rw'
                     },

  classID : CONTACTFIELD_CID,
  classInfo : XPCOMUtils.generateCI({classID: CONTACTFIELD_CID,
                                     contractID: CONTACTFIELD_CONTRACTID,
                                     classDescription: "ContactField",
                                     interfaces: [nsIDOMContactField],
                                     flags: nsIClassInfo.DOM_OBJECT}),

  QueryInterface : XPCOMUtils.generateQI([nsIDOMContactField])
}

//ContactTelField

const CONTACTTELFIELD_CONTRACTID = "@mozilla.org/contactTelField;1";
const CONTACTTELFIELD_CID        = Components.ID("{ed0ab260-e4aa-11e1-9b23-0800200c9a66}");
const nsIDOMContactTelField      = Components.interfaces.nsIDOMContactTelField;

function ContactTelField(aType, aValue, aCarrier) {
  this.type = sanitizeStringArray(aType);
  this.value = stringOrBust(aValue);
  this.carrier = stringOrBust(aCarrier);
};

ContactTelField.prototype = {
  __exposedProps__: {
                      type: 'rw',
                      value: 'rw',
                      carrier: 'rw'
                     },

  classID : CONTACTTELFIELD_CID,
  classInfo : XPCOMUtils.generateCI({classID: CONTACTTELFIELD_CID,
                                     contractID: CONTACTTELFIELD_CONTRACTID,
                                     classDescription: "ContactTelField",
                                     interfaces: [nsIDOMContactTelField],
                                     flags: nsIClassInfo.DOM_OBJECT}),

  QueryInterface : XPCOMUtils.generateQI([nsIDOMContactTelField])
}

//ContactFindSortOptions

const CONTACTFINDSORTOPTIONS_CONTRACTID = "@mozilla.org/contactFindSortOptions;1"
const CONTACTFINDSORTOPTIONS_CID        = Components.ID("{cb008c06-3bf8-495c-8865-f9ca1673a1e1}");
const nsIDOMContactFindSortOptions      = Ci.nsIDOMContactFindSortOptions;

function ContactFindSortOptions () { }

ContactFindSortOptions.prototype = {
  classID: CONTACTFINDSORTOPTIONS_CID,
  classInfo: XPCOMUtils.generateCI({classID: CONTACTFINDSORTOPTIONS_CID,
                                    contractID: CONTACTFINDSORTOPTIONS_CONTRACTID,
                                    classDescription: "ContactFindSortOptions",
                                    interfaces: [nsIDOMContactFindSortOptions],
                                    flags: nsIClassInfo.DOM_OBJECT}),
  QueryInterface: XPCOMUtils.generateQI([nsIDOMContactFindSortOptions])
};

//ContactFindOptions

const CONTACTFINDOPTIONS_CONTRACTID = "@mozilla.org/contactFindOptions;1";
const CONTACTFINDOPTIONS_CID        = Components.ID("{e31daea0-0cb6-11e1-be50-0800200c9a66}");
const nsIDOMContactFindOptions      = Components.interfaces.nsIDOMContactFindOptions;

function ContactFindOptions() { };

ContactFindOptions.prototype = {

  classID : CONTACTFINDOPTIONS_CID,
  classInfo : XPCOMUtils.generateCI({classID: CONTACTFINDOPTIONS_CID,
                                     contractID: CONTACTFINDOPTIONS_CONTRACTID,
                                     classDescription: "ContactFindOptions",
                                     interfaces: [nsIDOMContactFindSortOptions,
                                                  nsIDOMContactFindOptions],
                                     flags: nsIClassInfo.DOM_OBJECT}),

  QueryInterface : XPCOMUtils.generateQI([nsIDOMContactFindSortOptions,
                                          nsIDOMContactFindOptions])
}

//Contact

const CONTACT_CONTRACTID = "@mozilla.org/contact;1";
const CONTACT_CID        = Components.ID("{da0f7040-388b-11e1-b86c-0800200c9a66}");
const nsIDOMContact      = Components.interfaces.nsIDOMContact;

function checkBlobArray(aBlob) {
  if (Array.isArray(aBlob)) {
    for (let i = 0; i < aBlob.length; i++) {
      if (typeof aBlob != 'object') {
        return null;
      }
      if (!(aBlob[i] instanceof Components.interfaces.nsIDOMBlob)) {
        return null;
      }
    }
    return aBlob;
  }
  return null;
}

function isVanillaObj(aObj) {
  return Object.prototype.toString.call(aObj) == "[object Object]";
}

function validateArrayField(data, createCb) {
  if (data) {
    data = Array.isArray(data) ? data : [data];
    let filtered = [];
    for (let obj of data) {
      if (obj && isVanillaObj(obj)) {
        filtered.push(createCb(obj));
      }
    }
    return filtered;
  }
  return undefined;
}

function Contact() { };

Contact.prototype = {
  __exposedProps__: {
                      id: 'rw',
                      updated: 'rw',
                      published:  'rw',
                      name: 'rw',
                      honorificPrefix: 'rw',
                      givenName: 'rw',
                      additionalName: 'rw',
                      familyName: 'rw',
                      honorificSuffix: 'rw',
                      nickname: 'rw',
                      email: 'rw',
                      photo: 'rw',
                      url: 'rw',
                      category: 'rw',
                      adr: 'rw',
                      tel: 'rw',
                      org: 'rw',
                      jobTitle: 'rw',
                      bday: 'rw',
                      note: 'rw',
                      impp: 'rw',
                      anniversary: 'rw',
                      sex: 'rw',
                      genderIdentity: 'rw'
                     },

  set name(aName) {
    this._name = sanitizeStringArray(aName);
  },

  get name() {
    return this._name;
  },

  set honorificPrefix(aHonorificPrefix) {
    this._honorificPrefix = sanitizeStringArray(aHonorificPrefix);
  },

  get honorificPrefix() {
    return this._honorificPrefix;
  },

  set givenName(aGivenName) {
    this._givenName = sanitizeStringArray(aGivenName);
  },

  get givenName() {
    return this._givenName;
  },

  set additionalName(aAdditionalName) {
    this._additionalName = sanitizeStringArray(aAdditionalName);
  },

  get additionalName() {
    return this._additionalName;
  },

  set familyName(aFamilyName) {
    this._familyName = sanitizeStringArray(aFamilyName);
  },

  get familyName() {
    return this._familyName;
  },

  set honorificSuffix(aHonorificSuffix) {
    this._honorificSuffix = sanitizeStringArray(aHonorificSuffix);
  },

  get honorificSuffix() {
    return this._honorificSuffix;
  },

  set nickname(aNickname) {
    this._nickname = sanitizeStringArray(aNickname);
  },

  get nickname() {
    return this._nickname;
  },

  set photo(aPhoto) {
    this._photo = checkBlobArray(aPhoto);
  },

  get photo() {
    return this._photo;
  },

  set category(aCategory) {
    this._category = sanitizeStringArray(aCategory);
  },

  get category() {
    return this._category;
  },

  set email(aEmail) {
    this._email = validateArrayField(aEmail, function(email) {
      return new ContactField(email.type, email.value);
    });
  },

  get email() {
    return this._email;
  },

  set adr(aAdr) {
    this._adr = validateArrayField(aAdr, function(adr) {
      return new ContactAddress(adr.type, adr.streetAddress, adr.locality,
                                adr.region, adr.postalCode, adr.countryName);
    });
  },

  get adr() {
    return this._adr;
  },

  set tel(aTel) {
    this._tel = validateArrayField(aTel, function(tel) {
      return new ContactTelField(tel.type, tel.value, tel.carrier);
    });
  },

  get tel() {
    return this._tel;
  },

  set impp(aImpp) {
    this._impp = validateArrayField(aImpp, function(impp) {
      return new ContactField(impp.type, impp.value);
    });
  },

  get impp() {
    return this._impp;
  },

  set url(aUrl) {
    this._url = validateArrayField(aUrl, function(url) {
      return new ContactField(url.type, url.value);
    });
  },

  get url() {
    return this._url;
  },

  set org(aOrg) {
    this._org = sanitizeStringArray(aOrg);
  },

  get org() {
    return this._org;
  },

  set jobTitle(aJobTitle) {
    this._jobTitle = sanitizeStringArray(aJobTitle);
  },

  get jobTitle() {
    return this._jobTitle;
  },

  set note(aNote) {
    this._note = sanitizeStringArray(aNote);
  },

  get note() {
    return this._note;
  },

  set bday(aBday) {
    if (aBday !== undefined && aBday !== null) {
      this._bday = new Date(aBday);
    }
  },

  get bday() {
    return this._bday;
  },

  set anniversary(aAnniversary) {
    if (aAnniversary !== undefined && aAnniversary !== null) {
      this._anniversary = new Date(aAnniversary);
    }
  },

  get anniversary() {
    return this._anniversary;
  },

  set sex(aSex) {
    if (aSex !== "undefined") {
      this._sex = aSex;
    } else {
      this._sex = null;
    }
  },

  get sex() {
    return this._sex;
  },

  set genderIdentity(aGenderIdentity) {
    if (aGenderIdentity !== "undefined") {
      this._genderIdentity = aGenderIdentity;
    } else {
      this._genderIdentity = null;
    }
  },

  get genderIdentity() {
    return this._genderIdentity;
  },

  init: function init(aProp) {
    this.name =            aProp.name;
    this.honorificPrefix = aProp.honorificPrefix;
    this.givenName =       aProp.givenName;
    this.additionalName =  aProp.additionalName;
    this.familyName =      aProp.familyName;
    this.honorificSuffix = aProp.honorificSuffix;
    this.nickname =        aProp.nickname;
    this.email =           aProp.email;
    this.photo =           aProp.photo;
    this.url =             aProp.url;
    this.category =        aProp.category;
    this.adr =             aProp.adr;
    this.tel =             aProp.tel;
    this.org =             aProp.org;
    this.jobTitle =        aProp.jobTitle;
    this.bday =            aProp.bday;
    this.note =            aProp.note;
    this.impp =            aProp.impp;
    this.anniversary =     aProp.anniversary;
    this.sex =             aProp.sex;
    this.genderIdentity =  aProp.genderIdentity;
  },

  get published () {
    return this._published;
  },

  set published(aPublished) {
    this._published = aPublished;
  },

  get updated () {
    return this._updated;
  },

  set updated(aUpdated) {
    this._updated = aUpdated;
  },

  classID : CONTACT_CID,
  classInfo : XPCOMUtils.generateCI({classID: CONTACT_CID,
                                     contractID: CONTACT_CONTRACTID,
                                     classDescription: "Contact",
                                     interfaces: [nsIDOMContact, nsIDOMContactProperties],
                                     flags: nsIClassInfo.DOM_OBJECT}),

  QueryInterface : XPCOMUtils.generateQI([nsIDOMContact, nsIDOMContactProperties])
}

// ContactManager

const CONTACTMANAGER_CONTRACTID = "@mozilla.org/contactManager;1";
const CONTACTMANAGER_CID        = Components.ID("{0a2b72f3-7761-431b-8fa4-df1459316272}");
const nsIDOMContactManager      = Components.interfaces.nsIDOMContactManager;

function ContactManager()
{
  if (DEBUG) debug("Constructor");
}

ContactManager.prototype = {
  __proto__: DOMRequestIpcHelper.prototype,
  _oncontactchange: null,

  set oncontactchange(aCallback) {
    if (DEBUG) debug("set oncontactchange");
    let allowCallback = function() {
      if (!this._oncontactchange) {
        cpmm.sendAsyncMessage("Contacts:RegisterForMessages");
      }
      this._oncontactchange = aCallback;
    }.bind(this);
    let cancelCallback = function() {
      throw Components.results.NS_ERROR_FAILURE;
    }
    this.askPermission("listen", null, allowCallback, cancelCallback);
  },

  get oncontactchange() {
    return this._oncontactchange;
  },

  _setMetaData: function(aNewContact, aRecord) {
    aNewContact.id = aRecord.id;
    aNewContact.published = aRecord.published;
    aNewContact.updated = aRecord.updated;
  },

  _convertContact: function(aContact) {
    let newContact = new Contact();
    newContact.init(aContact.properties);
    this._setMetaData(newContact, aContact);
    return newContact;
  },

  _convertContacts: function(aContacts) {
    let contacts = [];
    for (let i in aContacts) {
      contacts.push(this._convertContact(aContacts[i]));
    }
    return contacts;
  },

  _fireSuccessOrDone: function(aCursor, aResult) {
    if (aResult == null) {
      Services.DOMRequest.fireDone(aCursor);
    } else {
      Services.DOMRequest.fireSuccess(aCursor, aResult);
    }
  },

  _pushArray: function(aArr1, aArr2) {
    aArr1.push.apply(aArr1, aArr2);
  },

  receiveMessage: function(aMessage) {
    if (DEBUG) debug("receiveMessage: " + aMessage.name);
    let msg = aMessage.json;
    let contacts = msg.contacts;

    let req;
    switch (aMessage.name) {
      case "Contacts:Find:Return:OK":
        req = this.getRequest(msg.requestID);
        if (req) {
          let result = this._convertContacts(contacts);
          Services.DOMRequest.fireSuccess(req.request, result);
        } else {
          if (DEBUG) debug("no request stored!" + msg.requestID);
        }
        break;
      case "Contacts:GetAll:Next":
        let data = this.getRequest(msg.cursorId);
        if (!data) {
          break;
        }
        let result = contacts ? this._convertContacts(contacts) : [null];
        if (data.waitingForNext) {
          if (DEBUG) debug("cursor waiting for contact, sending");
          data.waitingForNext = false;
          let contact = result.shift();
          this._pushArray(data.cachedContacts, result);
          this.nextTick(this._fireSuccessOrDone.bind(this, data.cursor, contact));
          if (!contact) {
            this.removeRequest(msg.cursorId);
          }
        } else {
          if (DEBUG) debug("cursor not waiting, saving");
          this._pushArray(data.cachedContacts, result);
        }
        break;
      case "Contacts:GetSimContacts:Return:OK":
        req = this.getRequest(msg.requestID);
        if (req) {
          let result = contacts.map(function(c) {
            let contact = new Contact();
            let prop = {name: [c.alphaId], tel: [ { value: c.number } ]};

            if (c.email) {
              prop.email = [{value: c.email}];
            }

            contact.init(prop);
            return contact;
          });
          if (DEBUG) debug("result: " + JSON.stringify(result));
          Services.DOMRequest.fireSuccess(req.request, result);
        } else {
          if (DEBUG) debug("no request stored!" + msg.requestID);
        }
        break;
      case "Contact:Save:Return:OK":
      case "Contacts:Clear:Return:OK":
      case "Contact:Remove:Return:OK":
        req = this.getRequest(msg.requestID);
        if (req)
          Services.DOMRequest.fireSuccess(req.request, null);
        break;
      case "Contacts:Find:Return:KO":
      case "Contact:Save:Return:KO":
      case "Contact:Remove:Return:KO":
      case "Contacts:Clear:Return:KO":
      case "Contacts:GetRevision:Return:KO":
      case "Contacts:Count:Return:KO":
      case "Contacts:GetSimContacts:Return:KO":
        req = this.getRequest(msg.requestID);
        if (req)
          Services.DOMRequest.fireError(req.request, msg.errorMsg);
        break;
      case "PermissionPromptHelper:AskPermission:OK":
        if (DEBUG) debug("id: " + msg.requestID);
        req = this.getRequest(msg.requestID);
        if (!req) {
          break;
        }

        if (msg.result == Ci.nsIPermissionManager.ALLOW_ACTION) {
          req.allow();
        } else {
          req.cancel();
        }
        break;
      case "Contact:Changed":
        // Fire oncontactchange event
        if (DEBUG) debug("Contacts:ContactChanged: " + msg.contactID + ", " + msg.reason);
        if (this._oncontactchange) {
          let event = new this._window.MozContactChangeEvent("contactchanged", {
            contactID: msg.contactID,
            reason: msg.reason
          });
          this._oncontactchange.handleEvent(event);
        }
        break;
      case "Contacts:Revision":
        if (DEBUG) debug("new revision: " + msg.revision);
        req = this.getRequest(msg.requestID);
        if (req) {
          Services.DOMRequest.fireSuccess(req, msg.revision);
        }
        break;
      default:
        if (DEBUG) debug("Wrong message: " + aMessage.name);
    }
    this.removeRequest(msg.requestID);
  },

  askPermission: function (aAccess, aRequest, aAllowCallback, aCancelCallback) {
    if (DEBUG) debug("askPermission for contacts");
    let access;
    switch(aAccess) {
      case "create":
        access = "create";
        break;
      case "update":
      case "remove":
        access = "write";
        break;
      case "find":
      case "getSimContacts":
      case "listen":
      case "revision":
        access = "read";
        break;
      default:
        access = "unknown";
      }

    // Shortcut for ALLOW_ACTION so we avoid a parent roundtrip
    let type = "contacts-" + access;
    let permValue =
      pm.testExactPermissionFromPrincipal(this._window.document.nodePrincipal, type);
    if (permValue == Ci.nsIPermissionManager.ALLOW_ACTION) {
      aAllowCallback();
      return;
    }

    let requestID = this.getRequestId({
      request: aRequest,
      allow: function() {
        aAllowCallback();
      }.bind(this),
      cancel : function() {
        if (aCancelCallback) {
          aCancelCallback()
        } else if (aRequest) {
          Services.DOMRequest.fireError(aRequest, "Not Allowed");
        }
      }.bind(this)
    });

    let principal = this._window.document.nodePrincipal;
    cpmm.sendAsyncMessage("PermissionPromptHelper:AskPermission", {
      type: "contacts",
      access: access,
      requestID: requestID,
      origin: principal.origin,
      appID: principal.appId,
      browserFlag: principal.isInBrowserElement
    });
  },

  save: function save(aContact) {
    let request;
    if (DEBUG) debug("save: " + JSON.stringify(aContact) + " :" + aContact.id);
    let newContact = {};
    newContact.properties = {
      name:            [],
      honorificPrefix: [],
      givenName:       [],
      additionalName:  [],
      familyName:      [],
      honorificSuffix: [],
      nickname:        [],
      email:           [],
      photo:           [],
      url:             [],
      category:        [],
      adr:             [],
      tel:             [],
      org:             [],
      jobTitle:        [],
      bday:            null,
      note:            [],
      impp:            [],
      anniversary:     null,
      sex:             null,
      genderIdentity:  null
    };
    for (let field in newContact.properties) {
      newContact.properties[field] = aContact[field];
    }

    let reason;
    if (aContact.id == "undefined") {
      // for example {25c00f01-90e5-c545-b4d4-21E2ddbab9e0} becomes
      // 25c00f0190e5c545b4d421E2ddbab9e0
      aContact.id = this._getRandomId().replace('-', '', 'g').replace('{', '').replace('}', '');
      reason = "create";
    } else {
      reason = "update";
    }

    this._setMetaData(newContact, aContact);
    if (DEBUG) debug("send: " + JSON.stringify(newContact));
    request = this.createRequest();
    let options = { contact: newContact, reason: reason };
    let allowCallback = function() {
      cpmm.sendAsyncMessage("Contact:Save", {requestID: this.getRequestId({request: request, reason: reason}), options: options});
    }.bind(this)
    this.askPermission(reason, request, allowCallback);
    return request;
  },

  find: function(aOptions) {
    if (DEBUG) debug("find! " + JSON.stringify(aOptions));
    let request = this.createRequest();
    let options = { findOptions: aOptions };
    let allowCallback = function() {
      cpmm.sendAsyncMessage("Contacts:Find", {requestID: this.getRequestId({request: request, reason: "find"}), options: options});
    }.bind(this)
    this.askPermission("find", request, allowCallback);
    return request;
  },

  createCursor: function CM_createCursor(aRequest) {
    let data = {
      cursor: Services.DOMRequest.createCursor(this._window, function() {
        this.handleContinue(id);
      }.bind(this)),
      cachedContacts: [],
      waitingForNext: true,
    };
    let id = this.getRequestId(data);
    if (DEBUG) debug("saved cursor id: " + id);
    return [id, data.cursor];
  },

  getAll: function CM_getAll(aOptions) {
    if (DEBUG) debug("getAll: " + JSON.stringify(aOptions));
    let [cursorId, cursor] = this.createCursor();
    let allowCallback = function() {
      cpmm.sendAsyncMessage("Contacts:GetAll", {
        cursorId: cursorId, findOptions: aOptions});
    }.bind(this);
    this.askPermission("find", cursor, allowCallback);
    return cursor;
  },

  nextTick: function nextTick(aCallback) {
    Services.tm.currentThread.dispatch(aCallback, Ci.nsIThread.DISPATCH_NORMAL);
  },

  handleContinue: function CM_handleContinue(aCursorId) {
    if (DEBUG) debug("handleContinue: " + aCursorId);
    let data = this.getRequest(aCursorId);
    if (data.cachedContacts.length > 0) {
      if (DEBUG) debug("contact in cache");
      let contact = data.cachedContacts.shift();
      this.nextTick(this._fireSuccessOrDone.bind(this, data.cursor, contact));
      if (!contact) {
        this.removeRequest(aCursorId);
      } else if (data.cachedContacts.length === CONTACTS_SENDMORE_MINIMUM) {
        cpmm.sendAsyncMessage("Contacts:GetAll:SendNow", { cursorId: aCursorId });
      }
    } else {
      if (DEBUG) debug("waiting for contact");
      data.waitingForNext = true;
    }
  },

  remove: function removeContact(aRecord) {
    let request;
    request = this.createRequest();
    let options = { id: aRecord.id };
    let allowCallback = function() {
      cpmm.sendAsyncMessage("Contact:Remove", {requestID: this.getRequestId({request: request, reason: "remove"}), options: options});
    }.bind(this)
    this.askPermission("remove", request, allowCallback);
    return request;
  },

  clear: function() {
    if (DEBUG) debug("clear");
    let request;
    request = this.createRequest();
    let options = {};
    let allowCallback = function() {
      cpmm.sendAsyncMessage("Contacts:Clear", {requestID: this.getRequestId({request: request, reason: "remove"}), options: options});
    }.bind(this)
    this.askPermission("remove", request, allowCallback);
    return request;
  },

  getSimContacts: function(aContactType) {
    let request;
    request = this.createRequest();
    let options = {contactType: aContactType};

    let allowCallback = function() {
      if (DEBUG) debug("getSimContacts " + aContactType);
      cpmm.sendAsyncMessage("Contacts:GetSimContacts",
        {requestID: this.getRequestId({request: request, reason: "getSimContacts"}),
         options: options});
    }.bind(this);

    let cancelCallback = function() {
      throw Components.results.NS_ERROR_NOT_IMPLEMENTED;
    }
    this.askPermission("getSimContacts", request, allowCallback, cancelCallback);
    return request;
  },

  getRevision: function() {
    let request = this.createRequest();

    let allowCallback = function() {
      cpmm.sendAsyncMessage("Contacts:GetRevision", {
        requestID: this.getRequestId(request)
      });
    }.bind(this);

    let cancelCallback = function() {
      Services.DOMRequest.fireError(request);
    };

    this.askPermission("revision", request, allowCallback, cancelCallback);
    return request;
  },

  init: function(aWindow) {
    // Set navigator.mozContacts to null.
    if (!Services.prefs.getBoolPref("dom.mozContacts.enabled"))
      return null;

    this.initHelper(aWindow, ["Contacts:Find:Return:OK", "Contacts:Find:Return:KO",
                              "Contacts:Clear:Return:OK", "Contacts:Clear:Return:KO",
                              "Contact:Save:Return:OK", "Contact:Save:Return:KO",
                              "Contact:Remove:Return:OK", "Contact:Remove:Return:KO",
                              "Contacts:GetSimContacts:Return:OK",
                              "Contacts:GetSimContacts:Return:KO",
                              "Contact:Changed",
                              "PermissionPromptHelper:AskPermission:OK",
                              "Contacts:GetAll:Next",
                              "Contacts:Revision"]);
  },

  // Called from DOMRequestIpcHelper
  uninit: function uninit() {
    if (DEBUG) debug("uninit call");
    if (this._oncontactchange)
      this._oncontactchange = null;
  },

  classID : CONTACTMANAGER_CID,
  QueryInterface : XPCOMUtils.generateQI([nsIDOMContactManager,
                                          Ci.nsIDOMGlobalPropertyInitializer,
                                          Ci.nsIMessageListener,
                                          Ci.nsISupportsWeakReference]),

  classInfo : XPCOMUtils.generateCI({classID: CONTACTMANAGER_CID,
                                     contractID: CONTACTMANAGER_CONTRACTID,
                                     classDescription: "ContactManager",
                                     interfaces: [nsIDOMContactManager],
                                     flags: nsIClassInfo.DOM_OBJECT})
}

this.NSGetFactory = XPCOMUtils.generateNSGetFactory(
                       [Contact, ContactManager, ContactProperties, ContactAddress, ContactField, ContactTelField, ContactFindSortOptions, ContactFindOptions])

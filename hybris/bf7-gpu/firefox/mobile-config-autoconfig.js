// Copyright 2022 Arnaud Ferraris, Oliver Smith
// SPDX-License-Identifier: MPL-2.0

// This is a Firefox autoconfig file:
// https://support.mozilla.org/en-US/kb/customizing-firefox-using-autoconfig

// Import custom userChrome.css on startup or new profile creation
const {classes: Cc, interfaces: Ci, utils: Cu} = Components;
// BF7: в Firefox 136+ модулей .jsm больше нет — Cu.import бросал исключение,
// и Firefox 140 при каждом старте показывал «Configuration Error: Failed to
// read the configuration file». Services теперь глобальный, FileUtils — ES-модуль.
var Services = globalThis.Services;
const { FileUtils } = ChromeUtils.importESModule("resource://gre/modules/FileUtils.sys.mjs");

var updated = false;

// Create <profile>/chrome/ directory if not already present
var chromeDir = Services.dirsvc.get("ProfD", Ci.nsIFile);
chromeDir.append("chrome");
if (!chromeDir.exists()) {
    chromeDir.create(Ci.nsIFile.DIRECTORY_TYPE, FileUtils.PERMS_DIRECTORY);
}

// Create nsIFile objects for userChrome.css in <profile>/chrome/ and in /etc/
var chromeFile = chromeDir.clone();
chromeFile.append("userChrome.css");
var defaultChrome = new FileUtils.File("/etc/mobile-config-firefox/userChrome.css");

// Remove the existing userChrome.css if older than the installed one
if (chromeFile.exists() && defaultChrome.exists() &&
    chromeFile.lastModifiedTime < defaultChrome.lastModifiedTime) {
    chromeFile.remove(false);
}

// Copy userChrome.css to <profile>/chrome/
if (!chromeFile.exists()) {
    defaultChrome.copyTo(chromeDir, "userChrome.css");
    updated = true;
}

// Create nsIFile objects for userContent.css in <profile>/chrome/ and in /etc/
var contentFile = chromeDir.clone();
contentFile.append("userContent.css");
var defaultContent = new FileUtils.File("/etc/mobile-config-firefox/userContent.css");

// Remove the existing userContent.css if older than the installed one
if (contentFile.exists() && defaultContent.exists() &&
    contentFile.lastModifiedTime < defaultContent.lastModifiedTime) {
    contentFile.remove(false);
}

// Copy userContent.css to <profile>/chrome/
if (!contentFile.exists()) {
    defaultContent.copyTo(chromeDir, "userContent.css");
    updated = true;
}

// Restart Firefox immediately if one of the files got updated
if (updated == true) {
    var appStartup = Cc["@mozilla.org/toolkit/app-startup;1"].getService(Ci.nsIAppStartup);
    appStartup.quit(Ci.nsIAppStartup.eForceQuit | Ci.nsIAppStartup.eRestart);
}

// Select a mobile user agent for firefox (same as tor browser on android)
// BF7: было «Android 10 … Firefox/91.0» (2021) — часть сайтов встречала
// предупреждением об устаревшем браузере. Версия — та же, что у нас (140 ESR).
defaultPref('general.useragent.override', 'Mozilla/5.0 (Android 14; Mobile; rv:140.0) Gecko/140.0 Firefox/140.0');

// Do not suggest facebook, ebay, reddit etc. in the urlbar. Same as
// Settings -> Privacy & Security -> Address Bar -> Shortcuts. As side-effect,
// the urlbar results are not immediatelly opened once clicking the urlbar.
defaultPref('browser.urlbar.suggest.topsites', false);

// Do not suggest search engines. Even though amazon is removed via
// policies.json, it gets installed shortly after the browser is opened. With
// this option, at least there is no big "Search with Amazon" message in the
// urlbar results as soon as typing the letter "a".
defaultPref('browser.urlbar.suggest.engines', false);

// Show about:home in new tabs, so it's not just a weird looking completely
// empty page.
defaultPref('browser.newtabpage.enabled', true);

// Default preferences for Mobian (can be overridden by users)

// Use smaller buttons
defaultPref("browser.uidensity", 1);

// Allow Firefox accounts
defaultPref("webchannel.allowObject.urlWhitelist", "https://accounts.firefox.com https://content.cdn.mozilla.net https://input.mozilla.org https://support.mozilla.org https://install.mozilla.org");

// Firefox is optimized for desktop.
// Some desktop optimizations might slow things on mobile.
// We should especially reduce CPU and RAM usage.

// Reduce the number of content processes.
//
// The more content processes you have,
// the more CPU and RAM will be assigned to each tab.
//
// Source: https://bit.ly/3dc4A7W
// Defualt value: 4
// Goal: Save CPU and RAM.
defaultPref("dom.ipc.processCount", 1);

// Disable prefetching.
//
// Source: https://bit.ly/3dc4A7W
// Default value: true
// Goal: Save bandwith
defaultPref("network.prefetch-next", false);

// Limit content redraws.
//
// While this helps the browser feel snappy,
// frequent redraws increase the total page load time,
// so a longer content notify interval will improve performance.
//
// Source: https://bit.ly/3hLX47p
// Default value: 120000 (0.12s) / true
// Goal: Speed up page loading
defaultPref("content.notify.interval", 500000);
defaultPref("content.notify.ontimer", true);

// Reduce the content switch threshold.
//
// If you haven't moved your mouse or touched the keyboard for the given time
// (the content switch threshold) then Firefox enters a low frequency interrupt mode,
// which means its interface becomes less responsive but your page loads more quickly.
//
// Source: https://bit.ly/3hLX47p
// Default value: 750000 (0.75s)
// Goal: Speed up page loading
defaultPref("content.switch.threshold", 250000);

// Reduce the number of visited web pages stored in memory,
// which you can access using the Back and Forward buttons.
//
// Source: https://bit.ly/3dc4A7W
// Default value: -1 (adaptable)
// Goal: Save RAM
defaultPref("browser.sessionhistory.max_total_viewers", 1);

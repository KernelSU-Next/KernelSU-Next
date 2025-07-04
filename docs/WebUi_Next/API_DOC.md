# WebUI-Next API Documentation

This document provides examples of how to use the `WebUI-Next` JavaScript APIs exposed to a module WebUI. These APIs allow code to run in the WebUI to interact with the system, execute shell commands, manage packages, control UI elements, and more coming soon.


## Table of Contents
1. [exec(cmd)](#exec-cmd)
2. [exec(cmd, callbackFunc)](#exec-cmd-callbackfunc)
3. [exec(cmd, options, callbackFunc)](#exec-cmd-options-callbackfunc)
4. [spawn(command, args, options, callbackFunc)](#spawn-command-args-options-callbackfunc)
5. [toast(msg)](#toast-msg)
6. [fullScreen(enable)](#fullscreen-enable)
7. [moduleInfo()](#moduleinfo)
8. [listSystemPackages()](#listsystempackages)
9. [listUserPackages()](#listuserpackages)
10. [listAllPackages()](#listallpackages)
11. [getPackagesInfo(packageNamesJson)](#getpackagesinfo-packagenamesjson)
12. [cacheAllPackageIcons(size)](#cacheallpackageicons-size)
13. [getPackagesIcons(packageNamesJson, size)](#getpackagesicons-packagenamesjson-size)

---

## exec(cmd)

Executes a shell command synchronously and returns the output as a string.

### Parameters
- `cmd` (String): The shell command to execute.

### Returns
- `String`: The command output (stdout).

### Example
```javascript
const output = ksu.exec("ls /system");
console.log("Output:", output);
```

---

## exec(cmd, callbackFunc)

Executes a shell command asynchronously and invokes the provided callback function with the result.

### Parameters
- `cmd` (String): The shell command to execute.
- `callbackFunc` (String): The name of the JavaScript callback function to invoke with the result.

### Callback Signature
```javascript
function callbackFunc(exitCode, stdout, stderr) {
  // Handle result
}
```

### Example
```javascript
function handleResult(exitCode, stdout, stderr) {
  console.log("Exit Code:", exitCode);
  console.log("Stdout:", stdout);
  console.log("Stderr:", stderr);
}

ksu.exec("ls /system", "handleResult");
```

---

## exec(cmd, options, callbackFunc)

Executes a shell command asynchronously with options (e.g., working directory, environment variables) and invokes the provided callback function with the result.

### Parameters
- `cmd` (String): The shell command to execute.
- `options` (String): A JSON string specifying options like `cwd` (working directory) and `env` (environment variables).
- `callbackFunc` (String): The name of the JavaScript callback function to invoke with the result.

### Options Format
```javascript
{
  "cwd": "/path/to/working/directory",
  "env": {
    "KEY1": "VALUE1",
    "KEY2": "VALUE2"
  }
}
```

### Callback Signature
```javascript
function callbackFunc(exitCode, stdout, stderr) {
  // Handle result
}
```

### Example
```javascript
const options = JSON.stringify({
  cwd: "/system",
  env: { PATH: "/system/bin" }
});

function handleResult(exitCode, stdout, stderr) {
  console.log("Exit Code:", exitCode);
  console.log("Stdout:", stdout);
  console.log("Stderr:", stderr);
}

ksu.exec("ls", JSON.stringify(options), "handleResult");
```

---

## spawn(command, args, options, callbackFunc)

Spawns a shell command with arguments and streams output through events to a JavaScript object.

### Parameters
- `command` (String): The shell command to execute.
- `args` (String): A JSON array of command arguments.
- `options` (String): A JSON string specifying options like `cwd` and `env` (optional).
- `callbackFunc` (String): The name of the JavaScript object to receive events (`stdout`, `stderr`, `exit`, `error`).

### Callback Object
The callback object should implement methods to handle events:
- `stdout.emit('data', data)`: Emits stdout data.
- `stderr.emit('data', data)`: Emits stderr data.
- `exit(code)`: Emits the exit code.
- `error(err)`: Emits an error object with `exitCode` and `message`.

### Example
```javascript
const streamHandler = {
  stdout: {
    emit: (event, data) => {
      if (event === "data") console.log("Stdout:", data);
    }
  },
  stderr: {
    emit: (event, data) => {
      if (event === "data") console.log("Stderr:", data);
    }
  },
  emit: (event, data) => {
    if (event === "exit") console.log("Exit Code:", data);
    if (event === "error") console.error("Error:", data);
  }
};

const args = JSON.stringify(["-l", "/system"]);
const options = JSON.stringify({ cwd: "/system" });

ksu.spawn("ls", args, options, "streamHandler");
```

---

## toast(msg)

Displays a short Android toast message.

### Parameters
- `msg` (String): The message to display.

### Example
```javascript
ksu.toast("Hello from WebUI-Next!");
```

---

## fullScreen(enable)

Toggles full-screen mode by hiding or showing system UI (status and navigation bars).

### Parameters
- `enable` (Boolean): `true` to enable full-screen mode, `false` to disable.

### Example
```javascript
// Enable full-screen
ksu.fullScreen(true);

// Disable full-screen
ksu.fullScreen(false);
```

---

## moduleInfo()

Returns information about the current module as a JSON string.

### Returns
- `String`: A JSON string containing module information, including `moduleDir` and other module-specific details.

### Example
```javascript
const moduleInfo = JSON.parse(ksu.moduleInfo());
console.log("Module Directory:", moduleInfo.moduleDir);
console.log("Module ID:", moduleInfo.id);
```

---

## listSystemPackages()

Returns a list of system package names as a JSON array.

### Returns
- `String`: A JSON array of system package names.

### Example
```javascript
const systemPackages = JSON.parse(ksu.listSystemPackages());
console.log("System Packages:", systemPackages);
```

---

## listUserPackages()

Returns a list of user-installed package names as a JSON array.

### Returns
- `String`: A JSON array of user package names.

### Example
```javascript
const userPackages = JSON.parse(ksu.listUserPackages());
console.log("User Packages:", userPackages);
```

---

## listAllPackages()

Returns a list of all installed package names as a JSON array.

### Returns
- `String`: A JSON array of all package names.

### Example
```javascript
const allPackages = JSON.parse(ksu.listAllPackages());
console.log("All Packages:", allPackages);
```

---

## getPackagesInfo(packageNamesJson)

Returns detailed information about specified packages as a JSON array.

### Parameters
- `packageNamesJson` (String): A JSON array of package names.

### Returns
- `String`: A JSON array of objects containing package details (`packageName`, `versionName`, `versionCode`, `appLabel`, `isSystem`, `uid`) or an error object if the package is not found.

### Example
```javascript
const packageNames = JSON.stringify(["com.android.settings", "com.example.app"]);
const packageInfos = JSON.parse(ksu.getPackagesInfo(packageNames));
packageInfos.forEach(info => {
  if (info.error) {
    console.error(`Error for ${info.packageName}: ${info.error}`);
  } else {
    console.log(`Package: ${info.packageName}, Version: ${info.versionName}, System: ${info.isSystem}`);
  }
});
```

---

## cacheAllPackageIcons(size)

Caches icons for all installed packages at the specified size.

### Parameters
- `size` (Number): The size (in pixels) for the square icon.

### Example
```javascript
// Cache all package icons at 48x48 pixels
ksu.cacheAllPackageIcons(48);
```

---

## getPackagesIcons(packageNamesJson, size)

Returns base64-encoded icons for specified packages as a JSON array.

### Parameters
- `packageNamesJson` (String): A JSON array of package names.
- `size` (Number): The size (in pixels) for the square icon.

### Returns
- `String`: A JSON array of objects containing `packageName` and `icon` (base64-encoded PNG or empty string if unavailable).

### Example
```javascript
const packageNames = JSON.stringify(["com.android.settings", "com.example.app"]);
const packageIcons = JSON.parse(ksu.getPackagesIcons(packageNames, 48));
packageIcons.forEach(item => {
  if (item.icon) {
    console.log(`Icon for ${item.packageName}: ${item.icon.substring(0, 30)}...`);
    // Example: Display icon in an <img> element
    const img = document.createElement("img");
    img.src = item.icon;
    document.body.appendChild(img);
  } else {
    console.log(`No icon for ${item.packageName}`);
  }
});
```

---

## Notes
- **Root Access**: Methods like `exec` and `spawn` require root access and use the `libsu` library for shell execution.
- **Asynchronous Operations**: Use `WebUI.post` to ensure UI thread safety when invoking JavaScript callbacks.
- **Error Handling**: Always check for errors in callbacks (e.g., `stderr` in `exec`, `error` event in `spawn`).
- **Icon Caching**: Use `cacheAllPackageIcons` to improve performance for subsequent `getPackagesIcons` calls.
- **JSON Parsing**: Ensure valid JSON strings are passed to methods like `getPackagesInfo` and `getPackagesIcons`.
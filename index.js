/**
 * @package node-ssh
 * @copyright  Copyright(c) 2011 Ajax.org B.V. <info AT ajax.org>
 * @author Gabor Krizsanits <gabor AT ajax DOT org>
 * @license http://github.com/ajaxorg/node-ssh/blob/master/LICENSE MIT License
 */

var ssh = require('./build/default/ssh');
//var constants = sftp.constants;
var fs = require("fs");
var path = require('path');
var constants = process.binding('constants');
var sys = require('sys');
var events = require('events');


/**
 *  Exported Object
 */
var SSH = module.exports = {};


/**
 *  Base class used by both ssh port forwarding and sftp
 */
function SSHBase(){
  this.setupEvent = function() {
    var _self = this;
    this._session.on('callback',function(error,result,more){
      //console.error("CALLBACK", _self._tasks[0].cmd);
      if (_self._timeout) {
        //console.error('clearTIMEOUT: ', _self._timeout);  
        clearTimeout(_self._timeout);
        _self._timeout = 0;
      }
      var cb = _self._tasks[0].cb;      
      _self._cb = true;
      if (cb && typeof cb == "function"){
        cb(error,result,more);  
      }  
      _self._tasks.shift();
      _self._cb = false;
      _self._executeNext();
    });    
  };
  
  this._addCommand = function(cmd, args, cb) {
    var task = {
        cmd:cmd, 
        args:args, 
        cb:cb
      };
    //console.error("add: ", cmd, this._tasks.length);      
      this._tasks.push(task);
      //console.error(this._tasks.length);
      if (this._tasks.length == 1 && !this._cb)
        this._executeNext();
  };
  
  this._executeNext = function() {
    var self = this;
    if (this._tasks.length>0) {
      //console.error('execute: ', this._tasks[0].cmd);
      if (this._timeout) {
        clearTimeout(this._timeout);
      }
      // setting a timeout, if it times out, we have to interrupt the blocking
      // operation, then trying to reconnect, if that fails too, we report the
      // error
      if (this._tasks[0].cmd != 'spawn')
      this._timeout = setTimeout(function(){
        clearTimeout(this._timeout);
        self._timeout = 0;        
        console.error("error: sftp timeout");
        self._session.removeAllListeners("callback");
        self._session.interrupt();
        if (self._reconnect) {
          self._reconnect = false;
          return self._lastCb("Error: SSH: Connection lost, reconnect timed out.");
        }
        self._recreate();
        var oldTasks = self._tasks;
        self._lastCb = self._tasks[0].cb;
        self._tasks = [];
        self.setupEvent();
        self._reconnect = true;
        self.init(self._options, function(err){
            console.error("ssh reinit");
            if (err)
              return self._lastCb(
                "Error: SFTP: Connection lost reconnect init error: " + err);
            
            self.connect(function(err){
              console.error("reconnect");
              return self._lastCb(
                "Error: SSH: Connection lost reconnect error: " + err);
                
              self._reconnect = false;
              self._tasks = oldTasks;
            });  
        });
      }, 25000);
      //console.error('setTIMEOUT: ', this._timeout);
      // if the command starts with '_' that means it's a javascript function 
      // to call, otherwise it's a c++ function on _session: 
      if (this._tasks[0].cmd.indexOf("_") === 0)
        this[this._tasks[0].cmd].apply(this, this._tasks[0].args);            
      else  {
        this._session[this._tasks[0].cmd].apply(this._session, this._tasks[0].args);      
      }
    }
    else 
    {
      // no commands left, let's send out a command after a while for
      // stay alive
      this._timeout = setInterval(function(){
        self.stat("");},100000);
    }
  };  
  
  this.init = function(options, cb) {        
    if (!options || 
        !options.host || 
        !options.user ||
        !options.port)
          throw new Error('Invalid argument.');
    if (!cb)
      cb = function(){};
    var self = this;  
    this._session.init(options);
    this._options = options;
    if (options.pubKey && options.prvKey) {      
      this.setPubKey(options.pubKey, function(error){
        if (error)
          return cb(error);
        self.setPrvKey(options.prvKey, function(error){
            self._inited = true;
            cb(error);
        });  
      }); 
    }
    else
     self._inited = true;
  };

  this.setPubKey = function(pub, cb) {
    var _self = this;
    if (!cb)
      cb = function(){};
    this._addCommand("_writeTmpFile", [pub], function(error, tmp) {
      if (error)
        return cb(error);
      _self._addCommand("setPubKey", [tmp], function(error) {
        fs.unlink(tmp);
        cb(error);
      });
    });  
  };
  
  this.setPrvKey = function(prv, cb) {
    var _self = this;
    if (!cb)
      cb = function(){};
    this._addCommand("_writeTmpFile", [prv], function(error, tmp) {
      if (error)
        return cb(error);
      _self._addCommand("setPrvKey", [tmp], function(error) {
        //fs.unlink(tmp);
        cb(error);
      });
    });      
  };
  
  this.connect = function(cb) {
    this._addCommand("connect", [], cb);
  };  
  
  this._openTmpFile = function() {
    var tmpFile = tmpDir + "/" + uuid();
    var _self = this;
    fs.open(tmpFile, "w+", function(error, fd) {      
      _self._session.emit("callback", error, {fd:fd, path:tmpFile});
    });
  };

  this._writeTmpFile = function(data) {
    var tmpFile = tmpDir + "/" + uuid();
    var _self = this;
    fs.writeFile(tmpFile, data, function(error) {      
      _self._session.emit("callback", error, tmpFile);
    });
  };

  this._readTmpFile = function(path) {
    var _self = this;
    fs.readFile(path, function(error, data) {
      _self._session.emit("callback", error, data);
      fs.unlink(path);
    });
  };  
}

var SFTP = SSH.sftp = function () {
  this._session = new ssh.SFTP();
  this._tasks = [];
  this._subTasks = [];
  this._callback = false;
  this._timeout = 0;
  this._options = {};
  this._stats = {};
  this.setupEvent();
};

(function(){
  SSHBase.call(this);
  
  this.isConnected = function() {
    return this._session.isConnected();
  };
  
  this._recreate = function() {
    this._session = new ssh.SFTP();  
  };
  
  /**
   * Async mkdir, the callback gets the error message on failure.
   * 
   * @param {String}        path
   * @param {String,octal}  mode
   * @param {Function}      cb
   * @type  {void}
   */     
  this.mkdir = function(path, mode, cb) {
    if (typeof path !== "string")
      throw new Error('Invalid argument.');
    if (typeof mode === "string")
      mode = parseInt(mode,10);
    if (typeof mode !== "number")
      throw new Error('Invalid argument.');
      
    this._addCommand("mkdir", [path, mode], cb);
  };

  /**
   * Asynchronously writes data to a file. data can be a string or a buffer.
   * Example:
   * <pre class="code">
   * sftp.writeFile("message.txt", "Hello Node", function(err) {
   *     if (err)
   *         throw err;
   *     console.log("It's saved!");
   * });
   * </pre>
   * 
   * @param {String}        filename
   * @param {String,Buffer} data
   * @param {String}        encoding
   * @param {Function}      callback
   * @type  {void}
   */
  this.writeFile = function(path, data, type, cb) {
    if (typeof data === 'string') {
      data = new Buffer(data);
      //console.log(data);
    }
    if (typeof type === 'function')
      cb = type;
      
    if (typeof path !== "string" || typeof data !== "object")
      throw new Error('Invalid argument.');

    this._addCommand("writeFile", [path, data], cb);
  };

  /**
   * Asynchronously reads the entire contents of a file.
   * The callback is passed two arguments (err, data), where data is the contents 
   * of the file.
   * If no encoding is specified, then the raw buffer is returned.
   * Example:
   * <pre class="code">
   * sftp.readFile("/etc/passwd", function(err, data) {
   *     if (err)
   *         throw err;
   *     console.log(data);
   * });
   * </pre>
   * 
   * @param {String}   filename
   * @param {String}   encoding
   * @param {Function} callback
   * @type  {void}
   */
  this.readFile = function(path, type, cb) {
    if (typeof path !== "string")
      throw new Error('Invalid argument.');    
    if (typeof type === 'function')
      cb = type;
      
    var _self = this;
    this._addCommand("_openTmpFile", [], function(error, tmp) {
      if (error)
        return cb(error);
      _self._addCommand("readFile", [tmp.fd, path], function(error) {
        if (error)
          return cb(error);
        fs.closeSync(tmp.fd);
        _self._addCommand("_readTmpFile", [tmp.path], function(error, data){
            if (error == null)
              error = "";
            fs.unlink(tmp.path);  
            return cb(error, data);  
          });
      });      
    });    
  };

  /**
   * Async list a directory, the callback gets two arguments, the error message 
   * on failure and the array of names of the children. '.' and '..' are exluded.
   * 
   * @param {String}       path
   * @param {Function}     cb
   * @type  {void}
   */  
  this.readdir = function(path,cb) {
    if (typeof path !== "string")
      throw new Error('Invalid argument.');    
    var self = this;
    this._addCommand("listDir", [path], function(err, entries, stats) {
      for (var i=0, l=entries.length; i<l; i++) {
        self._stats[path + '/' + entries[i]] = stats[i];
        //console.error("stat: ", i, stats[i], stats[i].isDirectory());
      }
      cb(err, entries);
    });
  };

  /**
   * Async rename, the callback gets the error message on failure.
   * 
   * @param {String}       path
   * @param {String}       to
   * @param {Function}     cb
   * @type  {void}
   */  
  this.rename = function(path, to, cb) {
    if (typeof path !== "string" || typeof to !== "string")
      throw new Error('Invalid argument.');    
    this._addCommand("rename", [path, to], cb);
  };
  
  /**
   * Async chomd, the callback gets the error message on failure.
   * 
   * @param {String}              path
   * @param {String, octal}       mode
   * @param {Function}            cb
   * @type  {void}
   */   
  this.chmod = function(path, mode, cb) {
    if (typeof mode === "string")
      mode = parseInt(mode,10);
    if (typeof path !== "string" || typeof mode !== "number")
      throw new Error('Invalid argument.');
    this._addCommand("chmod", [path, mode], cb);
  };

  /**
   * Async chown, the callback gets the error message on failure.
   * 
   * @param {String}          path
   * @param {String, number}  uid
   * @param {String, octal}   gid
   * @param {Function}        cb
   * @type  {void}
   */    
  this.chown = function(path, uid, gid, cb) {
    if (typeof uid === "string")
      uid = parseInt(mode,10);
    if (typeof gid === "string")
      gid = parseInt(mode,10);
    if (typeof path !== "string" || 
      typeof uid !== "number" ||
      typeof gid !== "number")
        throw new Error('Invalid argument.');
    this._addCommand("chown", [path, uid, gid], cb);
  };
  
  /**
   * Async stat, the callback gets two arguments, the error string and a stat
   * object. On error the stat object is undefined.
   * 
   * @param {String}       path
   * @param {Function}     cb
   * @type  {void}
   */    
  this.stat = function(path, cb) {
//    console.error("stat ", path);
    if (typeof path !== "string")
      throw new Error('Invalid argument.');
    if (path && this._stats[path])
      cb("", this._stats[path]);
    else
      this._addCommand("stat", [path], cb);  
  };
/*  
  this.lstat = function(path, cb) {
    this._addCommand("lstat", [path], cb);
  };
  
  this.sftptat = function(fd, cb) {
    this._addCommand("sftptat", [path], cb);
  };
*/  
  /**
   * Async delete file, the callback gets the error message on failure.
   * 
   * @param {String}       path
   * @param {Function}     cb
   * @type  {void}
   */  
  this.unlink = function(path, cb) {
    if (typeof path !== "string")
      throw new Error('Invalid argument.');
    this._addCommand("unlink", [path], cb);  
  };
  
  /**
   * Asynch remove directory. Directory must be empty. The callback gets the
   * error message on failure.
   * 
   * @param {String}       path
   * @param {Function}     cb
   * @type  {void}
   */  
  this.rmdir = function(path, cb) {
    if (typeof path !== "string")
      throw new Error('Invalid argument.');
    this._addCommand("rmdir", [path], cb);
  };
  
  /**
   * Executes a shell command on the server. The callback has two arguments,
   * the error if the command could not be executed, and the output of the 
   * command.
   * 
   * @param {String}       command
   * @param {Function}     cb
   * @type  {void}
   */  
  this.exec = function(command, cb) {
    if (typeof command !== "string")
      throw new Error('Invalid argument.');
    if (this._options.cwd)
        command = "cd " +this._options.cwd + "; " + command;
    this._addCommand("exec", [command], function(error, data){
      if (cb)
        cb(error, data ? data.join("") : data);
    });    
  };

  this.spawn = function(command, args, options) {
    if (typeof command !== "string")
      throw new Error('Invalid argument.');
    if (options && options.cwd)
      command = "cd " + options.cwd + " && " + command;
    if (options && options.env)
      for (var key in options.env)
        command = key + "=" + options.env[key] + " && " + command;      
    if (args)
      command = command + " '" + args.join("' '") + "'";
    var child = new Child(this);
    this._session.on("stderr", function(data){
      child.stderr.emit("data", data);
    });
    this._session.on("stdout", function(data){
      child.stdout.emit("data", data);
    });
    this._addCommand("spawn", [command], function(exit, error){
        console.log("FFS!!!!!!!!!!!!!!!");
        child.emit("exit", exit, error);
    });
    return child;
  };
  
}).call(SFTP.prototype);

var Tunnel = SSH.tunnel = function () {
  events.EventEmitter.call(this);
  this._session = new ssh.Tunnel();
  this._tasks = [];
  this._subTasks = [];
  this._callback = false;
  this._timeout = 0;
  this._options = {};
  this._stats = {};
  this.setupEvent();
};

sys.inherits(Tunnel, events.EventEmitter);

(function(){
  SSHBase.call(this);

  this._recreate = function() {
    this._session = new ssh.Tunnel();  
  };

  this.write = function(data, cb) {
    if (typeof data === 'string') {
      data = new Buffer(data);
      //console.log(data);
    }
      
    if (typeof data !== "object")
      throw new Error('Invalid argument.');

    this._addCommand("write", [data], cb);
  };
  
  this.read = function(cb) {    
    var self = this;
    this._addCommand("read", [], cb);
    //function(error, data) {
    //  if (error || data) {
    //      self.emmit("data", error, data);
    //  }  
    //});
  };
  
  this.startReading = function() {
      var self = this;
      var cb = function(){
        //console.log(".");
        self.read(function(err, data) {
          if (err) {
            console.log(err);            
          }  
          if (data) {
            //console.log(data.toString());                        
          }  
          self.emit("data", err, data);
          setTimeout(cb, data ? 10 : 500);
        });
      };
      setTimeout(cb,500);
  };
  
  this.connect = function(cb) {
    var self = this;
    this._addCommand("connect", [], function(err){
      self.startReading();
      cb(err);
    });
  };
}).call(Tunnel.prototype);


// UTILS

var tmpDir = (function() {
    var value,
        def     = "/tmp",
        envVars = ["TMPDIR", "TMP", "TEMP"],
        i       = 0,
        l       = envVars.length;
    for(; i < l; ++i) {
        value = process.env[envVars[i]];
        if (value)
            return fs.realpathSync(value).replace(/\/+$/, "");
    }
    return fs.realpathSync(def).replace(/\/+$/, "");
})();

var uuid = function(len, radix) {
   var i,
       chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz".split(""),
       uuid  = [],
       rnd   = Math.random;
   radix     = radix || chars.length;

   if (len) {
       // Compact form
       for (i = 0; i < len; i++)
           uuid[i] = chars[0 | rnd() * radix];
   }
   else {
       // rfc4122, version 4 form
       var r;
       // rfc4122 requires these characters
       uuid[8] = uuid[13] = uuid[18] = uuid[23] = "-";
       uuid[14] = "4";

       // Fill in random data.  At i==19 set the high bits of clock sequence as
       // per rfc4122, sec. 4.1.5
       for (i = 0; i < 36; i++) {
           if (!uuid[i]) {
               r = 0 | rnd() * 16;
               uuid[i] = chars[(i == 19) ? (r & 0x3) | 0x8 : r & 0xf];
           }
       }
   }

   return uuid.join("");
};

var Child = function(sftp){
  events.EventEmitter.call(this);
  this._sftp = sftp;
  this.stdout = new events.EventEmitter();
  this.stderr = new events.EventEmitter();
  this.kill = function(){
    this._sftp._session.kill();
  }
}

sys.inherits(Child, events.EventEmitter);

ssh.Stats.prototype._checkModeProperty = function(property) {
  return ((this.mode & constants.S_IFMT) === property);
};

ssh.Stats.prototype.isDirectory = function() {
  return this._checkModeProperty(constants.S_IFDIR);
};

ssh.Stats.prototype.isFile = function() {
  return this._checkModeProperty(constants.S_IFREG);
};

ssh.Stats.prototype.isBlockDevice = function() {
  return this._checkModeProperty(constants.S_IFBLK);
};

ssh.Stats.prototype.isCharacterDevice = function() {
  return this._checkModeProperty(constants.S_IFCHR);
};

ssh.Stats.prototype.isSymbolicLink = function() {
  return this._checkModeProperty(constants.S_IFLNK);
};

ssh.Stats.prototype.isFIFO = function() {
  return this._checkModeProperty(constants.S_IFIFO);
};

ssh.Stats.prototype.isSocket = function() {
  return this._checkModeProperty(constants.S_IFSOCK);
};


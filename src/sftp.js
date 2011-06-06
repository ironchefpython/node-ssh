var sftp = require('./build/default/sftp');
//var constants = sshd.constants;
var path = require('path');

var SFTP = module.exports = function () {
	var _session = new sftp.session();
	var _tasks = [];
	var _self;
	session.on('callback',function(error,result,more){
		_tasks[0].cb(error,result,more);
		if (!more) {
			_self.tasks[0].shift();
			_self._executeNext();
		}
	});
};

(function(){
	this._addCommand = function(cmd, args, cb) {
		_tasks.push({
			cmd:cmd, 
			args:args, 
			cb:cb
		});
		if (_tasks.length == 1)
			_self._executeNext();
	};
	
	this._executeNext = function() {
		if (_tasks.length>0)
			_session[_tasks[0].cmd](_tasks[0].args);
	};
	
	this.init = function(host, port, user, cb) {
		if (!host || typeof host !== 'string'
      || !port || typeof port !== 'number'
      || !user || typeof user !== 'string')
			  throw new Error('bad arguments');
		
		this._addCommand("init", [host, port, user], cb);
	};

	this.connect = function(password, keys, cb) {
		if (!password || typeof password != 'string')
      throw new Error('bad arguments');
      
    this._addCommand("connect", [password, keys], cb);
	};
	
	this.mkdir = function(name,cb) {
		this._addCommand("mkdir", [name], cb);
	};

}).call(SFTP.prototype);




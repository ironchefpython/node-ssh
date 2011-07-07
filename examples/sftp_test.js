var assert = require("assert");
var ssh = require('../index.js');
var basePath = 'sftptestdir';
var fs = require('fs');
var child_process = require('child_process');

var data = [];
for (var i = 0; i < 4000; i++)
data.push("some long data", i, "\r\n");
var longString = data.join("");

var prvkey = "-----BEGIN RSA PRIVATE KEY-----\n\
MIIEpQIBAAKCAQEAw0hN+bMuhMuHOOzakpmuf8OS6ieHVc7D8b0elXQZIptEOln2\n\
vwr506E69iqmh7UM6wbGPZSqlAEyqYq9zwkHKzFoJuHKtv/IDE5EcdV8DLR/+l1Q\n\
c+pnHFc4iZOdO/cG4qnldeiHMu1R2MWG2MgpO3/WH4HsWmwEZkjG7SYbbStQXaSg\n\
zDkitpKIt6BjSCjTKnVb3DadBGuQpx29lKvN86n7sH4wEGgkhifZoV77V3+T/1Fu\n\
nrgNxyVgz6/DNekP6vAcsR8x59ujUnHpPAKAHGCLFizlwt2OLwf2p//GAGS1Zgf9\n\
JpRhZAqCxDMz9y5bC/mp02NiRWPZtDd3nCzaRQIDAQABAoIBAQCyZXVGbVhL3Bq1\n\
+DpcvqRY93NZEa9ixjbeueQcqCjmIm2b2N+++unrWVkh1Si4xL7+Xfvv+cYy2z1L\n\
AQIRBrBT1xjMnGyx7Mz14PJKA7sFaEeZknGS00pK66ssk3uKcksJ+iczJa+M6Jxi\n\
qWBc3c49GrWjpu8iU5dZUZbYwn0/pjvu+pyb4olh5aIWyMiMPdPZBIXfVUMVb8NT\n\
y0LesnQH2RtOw7rY2fvb02djl+TvKstbAKERFigY2TQvyh8Jp3a3HUWIDKClEJkD\n\
cSaZt7peqWi9t3k8Ibu7elTk2yR5eEUjQyFyIblVaI77CXBjGXCQzk2wvNnr3NKX\n\
3jlm6gBpAoGBAP9WYGTmz1bIgSXxzsesmv1rrfiQ+lDwZukDYStG+zH82qdnsxf/\n\
r1SHmynWTfYz279vjPkWF1pFjX2dpj2Wm1LvrS5A6E/JbqSTtoyICdA+A+/TPh48\n\
iNSHmt2p+BUW9Q2PpRNUYqk6z2PJIyniWCBCTHXyFOLaLe4zdRU5T26fAoGBAMPK\n\
CGpbNdR/P6A1IEd+5ShaRGmLwSYJWbMpLWbk93eDyE/P8UnM61EV5Ae8f0boNKdk\n\
Ot4vHmQzVGRKRZhi0p+/rkEnpIGyqr9tSIKraNyEJir6r4jIChFqpdZvxziv6cPa\n\
+BJpTyYMMqT7SIBRMCU13Mqpfq9Fnzvyh6CqHyCbAoGBAI9tKJZlJEBePlVfH8T/\n\
iswhSUbfwQvoDhaDZHiX1ZA9tWDlmi8323fC+ICmtYI/nQdKlMhyBUoa2aCfBnt/\n\
9t2+bewWX6g5wOHHa3pDDCgiPbngUftQC5g+V9p9mDHYhGxKrPJPq1/d/hLSL+Ne\n\
FhyAwUxbYCoRXk14MCNs3taHAoGBAI9821oG6paHg3vIM5XyO8OtFAI+OBnGNIUH\n\
Io0MNQjT/dPwU6eAlNziLDI3RRgUSbJ71GDNK3rH24t8mzCpDC+jbPO3N+sNo/GT\n\
B9csBDfIaaiJ/GdEI4zMGinj1Z+H3Mx7B9+Gakk6G0uqFWJlHeHHbb7hJUUSwzZN\n\
8nQe+Z0NAoGAbSPinZEZVgBn2t8nNgU4The+l7KyQT8/bPT0C+PAHxLmnw/+xKRQ\n\
4978TJp/72fpFh8n9b4rosSjxFk2mxXZlM16eyOHZXpBT21agU9NbaJ4SEHj/5Ij\n\
ZFfOuDr1lUZW0pBL3lDt+kjkrx29K4WNMr7e7RJPv3vGwtyM75x6eRo=\n\
-----END RSA PRIVATE KEY-----";
var pubkey = "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQDDSE35sy6Ey4c47NqSma5/w5LqJ4dVzsPxvR6VdBkim0Q6Wfa/CvnToTr2KqaHtQzrBsY9lKqUATKpir3PCQcrMWgm4cq2/8gMTkRx1XwMtH/6XVBz6mccVziJk5079wbiqeV16Icy7VHYxYbYyCk7f9YfgexabARmSMbtJhttK1BdpKDMOSK2koi3oGNIKNMqdVvcNp0Ea5CnHb2Uq83zqfuwfjAQaCSGJ9mhXvtXf5P/UW6euA3HJWDPr8M16Q/q8ByxHzHn26NScek8AoAcYIsWLOXC3Y4vB/an/8YAZLVmB/0mlGFkCoLEMzP3LlsL+anTY2JFY9m0N3ecLNpF cloud9@vps6782.xlshosting.net";
var options = {
    host: "stage.io",
    port: 22,
    user: "sshtest",
    pubKey: pubkey,
    prvKey: prvkey
}

var httpRespond = "HTTP/1.1 200 OK\r\n\
content-type: text/html; charset=UTF-8\r\n\
Connection: keep-alive\r\n\
Transfer-Encoding: chunked\r\n\
\r\n\
20\r\n\
<html><body>muhaha</body></html>\r\n0\r\n\r\n"


var exp = module.exports = {
    deps: [],

    setUp: function (next) {            
        this.sftp.spawn("mkdir " + basePath).on("exit", function(){next()});
    },

    tearDown: function (next) {
        var sp = this.sftp.spawn("rm -rf " + basePath);
        sp.on("exit", function(){         
            next();
        });
        sp.stderr.on("data", function(data) {
            console.error('cleaning up failed: ', data);
        });
        sp.stdout.on("data", function(data) {
            console.error('cleaning up failed: ', data);
        });
    },

    "test mkdir": function (next) {
        var self = this;
        this.sftp.mkdir(basePath + '/testdir', "777", function (err) {
            assert.equal(err, "");
            self.sftp.mkdir(basePath + '/testdir/moretestdir', "777", function (err) {
                assert.equal(err, "");
                next();
            });
        });
    },
    
    "test rwFile": function (next) {
        var self = this;
        this.sftp.writeFile(basePath + "/testRWFile.txt", longString, function (err) {
            assert.equal(err, "");
            self.sftp.readFile(basePath + "/testRWFile.txt", function (err, data) {
                assert.equal(err, "");
                assert.equal(data.toString(), longString);
                self.sftp.writeFile("/testRWFile.txt", longString, function (err) {
                    assert.notEqual(err, "");
                    next();
                });    
            });
        });
    },

    "test listDir": function (next) {
        var self = this;
        this.sftp.writeFile(basePath + "/testFile1.txt", 'blah', function (err) {
            self.sftp.writeFile(basePath + "/testFile2.txt", 'blah', function (err) {
                self.sftp.writeFile(basePath + "/testFile3.txt", 'blah', function (err) {
                    self.sftp.writeFile(basePath + "/testFile4.txt", 'blah', function (err) {
                        self.sftp.readdir(basePath, function (err, files) {
                            assert.equal(err, "");
                            files = files.sort();
                            for (var v=0; v<4; v++)
                                assert.equal(files[v], "testFile" + (v+1) + ".txt");
                            next();
                        });
                    });
                });
            });
        });
    },

    "test rename": function (next) {
        var self = this;
        this.sftp.writeFile(basePath + "/torename.txt", 'blah', function (err) {
            self.sftp.rename(basePath + "/torename.txt", basePath + "/newName.txt", function (err) {
                assert.equal(err, "");
                self.sftp.readFile(basePath + "/newName.txt", function (err, data) {
                    assert.equal(err, "");
                    assert.equal(data.toString(), 'blah');
                    next();
                });
            });
        });    
    },

    "test chmod": function (next) {
        var self = this;
        this.sftp.writeFile(basePath + "/tochmod.txt", 'blah', function (err) {
            self.sftp.chmod(basePath + "/tochmod.txt", 777, function (err) {
                assert.equal(err, "");
                next();
            });
        });
    },    

    //    "test chown" : function(next) {
    //        fs.writeFileSync(basePath + "/tochown.txt", "a");
    //        this.sftp.chown(basePath + "/tochown.txt", gid, uid, 
    //          function(err) {
    //            assert.equal(err, "");
    //            next();
    //        });
    //    },
    
    "test stat": function (next) {
        var self = this;
        this.sftp.writeFile(basePath + "/stat.txt", 'blah', function (err) {
            self.sftp.stat(basePath + "/stat.txt", function (err, stat) {
                assert.equal(err, "");
                assert.equal(stat.isFile(), true);
                assert.equal(stat.isDirectory(), false);
                assert.equal(stat.isBlockDevice(), false);
                assert.equal(stat.isCharacterDevice(), false);
                assert.equal(stat.isFIFO(), false);
                assert.equal(stat.isSocket(), false);
                //            assert.equal(stat.ino, fsstat.ino);
                assert.equal(stat.mode, 33216);
                assert.equal(stat.uid, 208001);
                assert.equal(stat.gid, 1);
                assert.equal(stat.size, 4);
                //assert.equal(stat.atime.toString(), "");
                //assert.equal(stat.mtime.toString(), "");
                //            assert.equal(stat.ctime.toString(), fsstat.ctime.toString());
                next();
            });
        });    
    },

    "test unlink": function (next) {
        var self = this;
        this.sftp.writeFile(basePath + "/torem.txt", 'blah', function (err) {
            self.sftp.unlink(basePath + "/torem.txt", function (err) {
                assert.equal(err, "");
                //check if exist
                next();
            });
        });    
    },

    "test rmdir": function (next) {
        var self = this;
        this.sftp.mkdir(basePath + "/torem", "777", function (err) {
            self.sftp.rmdir(basePath + "/torem", function (err) {
                assert.equal(err, "");
                //check if exist
                next();
            });
        });    
    },
    
    "test spawn": function (next) {
      var self = this; 
      var script = 
      "for(var v=0; v<4000; v++){\
         console.log('some long data'+ v);\
      }";
      this.sftp.writeFile(basePath + "/runme.js", script, function (err) {
        self.sftp.chmod(basePath + "/runme.js", 777, function (err) {
            var child = self.sftp.spawn("node", [basePath + "/runme.js"]);
            var o = [],e = [];
            child.stdout.on("data", function(data) {
                o.push(data.toString());
            });
            child.stderr.on("data", function(data) {
                e.push(data.toString());
            });
            child.on("exit", function(code,error){
                fs.writeFileSync("./blah.txt", o.join(""));    
                assert.equal(error, "");
                assert.equal(code, 0, "error code is wrong");
                assert.equal(o.join(""), longString, "stdout failed");
                next();
            });
        });
      });    
    },
    
    "test spawn": function (next) {
      var self = this; 
      var script = 
      "for(var v=0; v<4000; v++){\
         console.log('some long data'+ v);\
      }";
      this.sftp.writeFile(basePath + "/runme.js", script, function (err) {
        self.sftp.chmod(basePath + "/runme.js", 0777, function (err) {
            var child = self.sftp.spawn("node", [basePath + "/runme.js"]);
            var o = [],e = [];
            child.stdout.on("data", function(data) {
                o.push(data.toString());
            });
            child.stderr.on("data", function(data) {
                e.push(data.toString());
            });
            child.on("exit", function(code,error){  
                assert.equal(error, "");
                assert.equal(code, 0, "error code is wrong");
                assert.equal(o.join(""), longString, "stdout failed");
                next();
            });
        });
      });    
    },    
    
    // writes a js file to the server with sftp, then executes it on the server,
    // (spawn) the scripts opens a http server, then with tunnel, the server
    // conencts to that http server as a client, and does a http get on it    
    "test tunnel": function (next) {
      var self = this; 
      var script = 
        "var http = require('http');\
        http.createServer(function(request, response) {\
            if (request.url === '/favicon.ico') {\
                response.writeHead(404);\
        		response.end();\
        		return;\
        	}\
        	console.log('attached');\
            response.writeHead(200, {\
            	'content-type': 'text/html; charset=UTF-8'\
        	});\
        	response.end('<html><body>muhaha</body></html>');\
        }).listen(11000);\
        process.on('uncaughtException', function(err) {\
        	console.error(err.stack);\
        });";
      var tunnel = new ssh.tunnel(); 
      var options2 = {
        host: "stage.io",
        port: 22,
        user: "sshtest",
        pubKey: pubkey,
        prvKey: prvkey,
        remoteHost: "localhost",
        remotePort: 11000,        
      }
      this.sftp.writeFile(basePath + "/runme.js", script, function (err) {
        self.sftp.chmod(basePath + "/runme.js", 0777, function (err) {
            var child = self.sftp.spawn("node", [basePath + "/runme.js"]);
            var o = [],e = [];
            child.stdout.on("data", function(data) {
                assert.equal(data.toString(), "attached\r\n");
                o.push(data.toString());
            });
            child.stderr.on("data", function(data) {
                e.push(data.toString());
            });
            child.on("exit", function(code,error){
                //next();
            });
            tunnel.init(options2, function(err){
                assert.equal(err, "");
                tunnel.on("data", function(err, data){
                    assert.equal(err, "")
                    assert.equal(data.toString(), httpRespond);
                    child.kill();
                    next();
                })
                tunnel.connect(function(err){
                    assert.equal(err, "");
                    tunnel.write("GET / HTTP/1.1\nHost: localhost:11000\n\n",function(err){
                       assert.equal(err,""); 
                    });
                })
            });
        });
      });    
    },    
    
    // same test as above except we kill the remote process before we try to
    // connect to it
    "test kill": function (next) {
      var self = this; 
      var script = 
        "var http = require('http');\
        console.error('start');\
        http.createServer(function(request, response) {\
            if (request.url === '/favicon.ico') {\
                response.writeHead(404);\
            	response.end();\
        		return;\
        	}\
            response.writeHead(200, {\
            	'content-type': 'text/html; charset=UTF-8'\
        	});\
        	response.end('<html><body>muhaha</body></html>');\
        }).listen(11001);\
        process.on('uncaughtException', function(err) {\
        	console.error(err.stack);\
        });";
      var tunnel = new ssh.tunnel(); 
      var options2 = {
        host: "stage.io",
        port: 22,
        user: "sshtest",
        pubKey: pubkey,
        prvKey: prvkey,
        remoteHost: "localhost",
        remotePort: 11001,        
      }
      this.sftp.writeFile(basePath + "/runme3.js", script, function (err) {
        assert.equal(err, "");  
        self.sftp.chmod(basePath + "/runme3.js", 0777, function (err) {
            assert.equal(err, "");
            var child = self.sftp.spawn("node", [basePath + "/runme3.js"]);
            var o = [],e = [];
            child.stdout.on("data", function(data) {
                assert.equal(data.toString(), "start\r\n");
                child.kill();
                tunnel.init(options2, function(err){
                    assert.equal(err, "");
                    // since we killed the http server (remote) process this should fail
                    tunnel.connect(function(err){
                        assert.equal(err, "Error opening a channel for port forwarding: Channel opening failure: channel 43 error (1) open failed\n");
                        next();
                    })
                });                
            });
            child.stderr.on("data", function(data) {
                console.error('err', data.toString());
                e.push(data.toString());
            });
            child.on("exit", function(code,error){
                // the child process has been killed:
                assert.equal(code, -1);
                assert.equal(error,"");
            });            
        });
      });    
    },    
};

exp.sftp = new ssh.sftp();
exp.sftp.init(options, function (err) {
    assert.equal(err, "");
    exp.sftp.connect(function (err) {
        assert.equal(err, "");
        console.log("connected");
        require("../../../../../../../server/c9/test").testcase(
            module.exports, "SFTP", 10000).deps().exec();        
    });
});
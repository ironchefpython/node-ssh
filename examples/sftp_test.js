var assert = require("../../../../cloud9test/server/c9/test/assert");
var sftp = require('../index.js');
var basePath = '/Development/sftptestdir';
var fs = require('fs');
var child_process = require('child_process');

var data = [];
for (var i=0; i<4000; i++)
  data.push("some long data", i);
var longString = data.join("");  

module.exports = {    
    deps : [],
  
    setUp : function(next) {
      fs.mkdirSync(basePath,parseInt('777',8));
      this.sftp = new sftp();
      this.sftp.init({host: "localhost",
             port: 22,
             user: "gabor"});
          
      this.sftp.connect(function(err){
        assert.equal(err,"");
        next();
      });
    },
    
    tearDown : function(next) {
     child_process.exec("rm -r " + basePath)  
    },
    
    "test mkdir" : function(next) {
      this.sftp.mkdir(basePath + '/testDir', function(err) {
        assert.equal(err, "");
        assert.equal(fs.statSync(basePath + '/testDir').isDirectory(), true);
        next();
      });
    },
    
    "test writeFile" : function(next) {
        this.sftp.writeFile(basePath + "/testWriteFile.txt", longString, 
          function(err) {
            assert.equal(err, "");
            assert.equal(fs.readFileSync(basePath + "/testWriteFile.txt"), 
              longString);
            next();
        });
    },
    
    "test readFile" : function(next) {
        fs.writeFileSync(basePath + "/testReadFile.txt", longString);
        this.sftp.readFile(basePath + "/testReadFile.txt", 
          function(err, data) {            
            assert.equal(err, "");
            assert.equal(data.toString(), longString);
            next();
        });
    },

    "test listDir" : function(next) {
        fs.writeFileSync(basePath + "/testFile1.txt", "a");
        fs.writeFileSync(basePath + "/testFile2.txt", "a");
        fs.writeFileSync(basePath + "/testFile3.txt", "a");
        fs.writeFileSync(basePath + "/testFile4.txt", "a");
        this.sftp.readdir(basePath, 
          function(err, files) {
            assert.equal(err, "");
            assert.sameItems(files, 
              ["testFile1.txt", "testFile2.txt", 
              "testFile3.txt", "testFile4.txt"]);
            next();
        });
    },

    "test rename" : function(next) {
        fs.writeFileSync(basePath + "/torename.txt", "a");
        this.sftp.rename(basePath + "/torename.txt", basePath + "/newName.txt", 
          function(err) {
            assert.equal(err, "");
            assert.equal(fs.statSync(basePath + '/newName.txt').isFile(), 
              true);
            next();
        });
    },
    
    "test chmod" : function(next) {
        fs.writeFileSync(basePath + "/tochmod.txt", "a");
        this.sftp.chmod(basePath + "/tochmod.txt", 777, 
          function(err) {
            assert.equal(err, "");
            next();
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

    "test stat" : function(next) {
        fs.writeFileSync(basePath + "/stat.txt", "a");
        this.sftp.stat(basePath + "/stat.txt", 
          function(err, stat) {
            assert.equal(err, "");
            var fsstat = fs.statSync(basePath + "/stat.txt");
            assert.equal(stat.isFile(), fsstat.isFile());
            assert.equal(stat.isDirectory(), fsstat.isDirectory());
            assert.equal(stat.isBlockDevice(), fsstat.isBlockDevice());
            assert.equal(stat.isCharacterDevice(), fsstat.isCharacterDevice());
            assert.equal(stat.isFIFO(), fsstat.isFIFO());
            assert.equal(stat.isSocket(), fsstat.isSocket());
//            assert.equal(stat.ino, fsstat.ino);
            assert.equal(stat.mode, fsstat.mode);
            assert.equal(stat.uid, fsstat.uid);
            assert.equal(stat.gid, fsstat.gid);
            assert.equal(stat.size, fsstat.size);
            assert.equal(stat.atime.toString(), fsstat.atime.toString());
            assert.equal(stat.mtime.toString(), fsstat.mtime.toString());
//            assert.equal(stat.ctime.toString(), fsstat.ctime.toString());
            next();
        });
    },
    
    "test unlink" : function(next) {
        fs.writeFileSync(basePath + "/torem.txt", "a");
        this.sftp.unlink(basePath + "/torem.txt", 
          function(err) {
            assert.equal(err, "");
            //check if exist
            next();
        });
    },

    "test rmdir" : function(next) {
        fs.mkdir(basePath + "/torem", parseInt('777',8));
        this.sftp.rmdir(basePath + "/torem", 
          function(err) {
            assert.equal(err, "");
            //check if exist
            next();
        });
    },
};

require("../../cloud9test/server/c9/test").testcase(module.exports, "SFTP", 300).deps().exec();
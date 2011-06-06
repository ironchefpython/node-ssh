var sftp = require('../index.js');
var session = new sftp();

session.init({host: "localhost",
             port: 22,
             user: "gabor"
             });


session.connect(function(error){
  console.log("connect done");  
  session.mkdir("/Development/testdir",function(error){
      console.log("mkdir done");
      session.writeFile("/Development/testdir/tesfile", 
        "some data, it could be a buffer, actually it is always converted into a buffer",
        function(error){
          console.log("write to file done");
          session.readFile("/Development/testdir/tesfile", function(error, data){
            console.log("read from file done: ", data.toString());      
          });   
        });        
  });  
  
  session.readdir("/Development/testdir", function(error, dirs){
      console.log("readdir done: ",dirs);
  });

  session.readFile("/Development/testdir/wargarbl", function(error, data){
    console.log("readFile failed: ", error);      
  });  
});
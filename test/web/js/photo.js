//var image1 = $('#image');
var cropper = new Cropper(image, {
  aspectRatio:9/16,
  viewMode: 1,
  ContainerWidth: 350,
  ContainerHeight: 350,
  dragMode: 'move',
  autoCropArea: 0.8, 
  restore: false,
  modal: false,
  guides: true,
  highlight: false,
  cropBoxMovable: true,
  cropBoxResizable: true,
  toggleDragModeOnDblclick: false,
});
$('#selectImg').on('change',function(e){
  var file = e.target.files[0];
  console.log("ifeng = "+file.name);
  var format = file.name.toLowerCase().substring(file.name.lastIndexOf('.') + 1);
  if(format != "jpg" && format != "jpeg") return;
  $(".cut-img-model").toggle(); 
  var reader = new FileReader();  
  reader.onload = function(evt) {  
    var replaceSrc = evt.target.result;
    // 更换cropper的图片
    cropper.replace(replaceSrc, false);
  }
  reader.readAsDataURL(file);
})
$('#rotate').on('click', function() {
  cropper.rotate(90);
})	
$('#crop').on('click', function() {
  $(".cut-img-model").toggle(); 
  var src = cropper.getCroppedCanvas({width:135,height:240}).toDataURL('image/jpeg', 0.99);
  //console.log(src); // base64格式
})
$('#cancel').on('click', function() {
  $(".cut-img-model").toggle();
})	
function listDir(obj){
  var path = $(obj).attr("path");
  //console.log(path); 
  var xhr = new XMLHttpRequest();
  var fnstring = String("/filelist?dir=")+path;
  xhr.open("GET", fnstring, true);
  //console.log(fnstring);
  xhr.send();
  //setTimeout("location.href = '../filesystem';", 3000);
  xhr.onreadystatechange = function() {
    if (xhr.readyState == XMLHttpRequest.DONE) {
      //console.log(xhr.responseText);
      $("#list").replaceWith(xhr.responseText);
      $("#pwd").html(path);
      
      $("#btnback").attr("path", path);
    }
  }
}

function refreshPwd(){
  var path = $('#pwd').html();
  if(path == "") path = "/image";
  var xhr = new XMLHttpRequest();
  var fnstring = String("/filelist?dir=")+path;
  xhr.open("GET", fnstring, true);
  //console.log(fnstring);
  xhr.send();
  xhr.onreadystatechange = function() {
    if (xhr.readyState == XMLHttpRequest.DONE) {
      //console.log(xhr.responseText);
      if(xhr.responseText != "")
      $("#list").replaceWith(xhr.responseText);
    }
  }
  var xh = new XMLHttpRequest();
  var fnstring = String("/space.json");
  xh.open("GET", fnstring, true);
  xh.send();
  xh.onreadystatechange = function() {
    if (xh.readyState == XMLHttpRequest.DONE) {
      console.log(xh.responseText);
      var rsp = JSON.parse(xh.responseText)
      console.log(rsp.free);
      if(rsp.free != ""){							
        getE("free").innerText = "Free Space:"+ Math.floor(rsp.free/1024) +"KB";
      }						
    }
  }	
}
refreshPwd();
function deletef(h) {
    var xhr = new XMLHttpRequest();
    var fnstring=String("/delete?file=")+encodeURIComponent(h);
    xhr.open("GET", fnstring, true);
    console.log(fnstring);
  xhr.send();
  xhr.onreadystatechange = function() {
    if (xhr.readyState == XMLHttpRequest.DONE) {
      alert("delete ok");
      console.log(xhr.responseText);
      refreshPwd();
    }
  }
}

function setgif(f){
  var url = String("/set?img=")+encodeURIComponent(f);
  send_http(url);
}				

function sub(obj){
  var a = obj.value;
  console.log(a);
  var fileName = a.replace(/^.*[\\\/]/, '');
  fileName = fileName.replace(/&/g, '_');
  console.log(fileName);
  var len = fileName.length;
  if(fileName.length>30){
  fileName = fileName.substring(len-20);
  console.log(fileName);
  }
  getE('file-input').innerHTML = fileName;
  getE('updateBtn').disabled = false;
  getE('prgbar').style.display = 'block';
  getE('prg').style.display = 'block';
};

$('#upload_form').submit(function(e){
  getE('updateBtn').disabled = "disabled";  
  e.preventDefault();
  var form = $('#upload_form')[0];
  var data = new FormData(form);
  var dir = $("#pwd").html();
  var filename = getE('file-input').innerHTML;
  var format = filename.toLowerCase().substring(filename.lastIndexOf('.') + 1);
  if(format != "jpg" && format != "jpeg") {
  console.log("upload not jpg files.");
    $.ajax({
      url: '/doUpload?dir='+dir,
      type: 'POST',
      data: data,
      contentType: false,
      processData:false,
      xhr: function() {
        var xhr = new window.XMLHttpRequest();
        xhr.upload.addEventListener('progress', function(evt) {
          if (evt.lengthComputable) {
            var per = evt.loaded / evt.total;
            $('#prg').html('Progress: ' + Math.round(per*100));
            $('#bar').css('width', Math.round(per*350)+"px");
            }
        }, false);
        return xhr;
      },
      success:function(d, s) {
        console.log('success!'); 
        getE('prgbar').style.display = 'none';
        getE('prg').style.display = 'none';
        $('#prg').html('Ready to upload');
        $('#bar').css('width:0px');
        getE('file-input').innerHTML = 'Choose ...';
        getE('updateBtn').disabled = true;

        refreshPwd();
      },
      error: function (a, b, c) {
        alert("Upload error");
      }
    });
  }else{
    var canvas = cropper.getCroppedCanvas({width:135,height:240});
    var src = cropper.getCroppedCanvas({width:135,height:240}).toDataURL('image/jpeg', 0.99);
    //data.append("file", encodeURIComponent(src)); 
    canvas.toBlob(function (blob){
      var formData = new FormData();
      formData.append('file', blob, getE('file-input').innerHTML);
      $.ajax({
        url: '/doUpload?dir='+dir,
        type: 'POST',
        data: formData,
        contentType: false,
        processData:false,
        xhr: function() {
          var xhr = new window.XMLHttpRequest();
          xhr.upload.addEventListener('progress', function(evt) {
            if (evt.lengthComputable) {
              var per = evt.loaded / evt.total;
              $('#prg').html('Progress: ' + Math.round(per*100));
              $('#bar').css('width', Math.round(per*350)+"px");
              }
          }, false);
          return xhr;
        },
        success:function(d, s) {
          console.log('success!'); 
          getE('prgbar').style.display = 'none';
          getE('prg').style.display = 'none';
          $('#prg').html('Ready to upload');
          $('#bar').css('width:0px');
          getE('file-input').innerHTML = 'Choose...';
          getE('updateBtn').disabled = true;

          refreshPwd();
        },
        error: function (a, b, c) {
          alert("Upload OK");
          //setTimeout("location.href = '/fs.html';", 1000);
          refreshPwd();
        }
      });
    
  
      console.log("upload "+dir);
    },"image/jpeg", 0.99);
  }
});

function set_image(){
  var autoplay = getE("autoplay");
  if(autoplay.checked) autoplay.value=1;
  else autoplay.value=0;
  var image_interval = getE("image_interval");
  var url = "/set?i_i="+image_interval.value+"&autoplay=1";
  console.log(url);
  send_http(url);	
}
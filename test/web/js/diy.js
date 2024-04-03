var croppers = [];

// 修改文件选择的change事件
$("input[type='file']").on("change", function() {
  var fileInput = this;
  var index = $(this).attr("id").replace("file", "");
  var image = $("#image" + index);
  var cropButton = $("#cropButton" + index);
  var rotateButton = $("#rotateButton" + index);
  var fileLabel = $(".file-label[for='file" + index + "']");
  var cropImage = $("#crop_image" + index);
  cropImage.hide();
  $("#image_div" + index).show();
  cropButton.text("crop&upload");
  cropButton.prop("disabled", false);
  if (fileInput.files.length > 0) {
    var file = fileInput.files[0];
    if (file.type === "image/jpeg" || file.type === "image/jpg" || file.type === "image/png" || file.type === "image/bmp") {
      var reader = new FileReader();

      reader.onload = function (e) {
        image.attr("src", e.target.result);

        croppers[index - 1] = new Cropper(image[0], {
          aspectRatio: 9 / 16,
          viewMode: 1,
        });

        cropButton.show();
        rotateButton.show();
        //fileLabel.hide();
      };

      reader.readAsDataURL(file);
    } else {
      showMessage("Please choose jpg/png/jpeg/bmp images", true);
      fileInput.value = "";
    }
  }
});

$(".crop-button").on("click", function() {
  var index = $(this).attr("id").replace("cropButton", "");

  if (croppers[index - 1]) {
      var croppedImageData = croppers[index - 1].getCroppedCanvas({ width: 135, height: 240 }).toDataURL("image/jpeg", 1);
      croppers[index - 1].destroy();

      //$(this).hide();
      $("#rotateButton" + index).hide();
      
      var cropImage = $("#crop_image" + index);
      var srcImage = $("#image" + index);
      var cropButton = $("#cropButton" + index);
      srcImage.hide();
      cropImage.attr("src", croppedImageData);
      cropImage.show();
      cropButton.text("uploading...");
      cropButton.prop("disabled", true);

      var fileName = "tube" + index + ".jpg";
      var croppedImageData = $("#crop_image" + index)[0].src;

      // Convert base64 string to Blob
      var byteCharacters = atob(croppedImageData.split(',')[1]);
      var byteNumbers = new Array(byteCharacters.length);
      for (var i = 0; i < byteCharacters.length; i++) {
        byteNumbers[i] = byteCharacters.charCodeAt(i);
      }
      var byteArray = new Uint8Array(byteNumbers);
      //var blob = new Blob([byteArray], { type: 'image/jpeg' });
      var blob = dataURItoBlob(croppedImageData, { quality: 1 });

      // Create a FormData object and append the blob with a filename
      var formData = new FormData();
      formData.append('file', blob, fileName);
      //formData.append("fileName", fileName);

      $.ajax({
        url: "/doUpload?dir=/diy",
        type: "POST",
        data: formData,
        processData: false,
        contentType: false,
        success: function(response) {
          showMessage("Upload success!", false);
          //cropButton.text("Crop&Upload");
          cropButton.hide();
        },
        error: function() {
          showMessage("Upload failed. Please try again.", true);
          //cropButton.text("Crop&Upload");
          //uploadButton.prop("disabled", false);
          cropButton.hide();
        }
    });
  } else {
    showMessage("invalid image or cropper fail",true);
  }
});
// Function to convert data URI to Blob
function dataURItoBlob(dataURI, options) {
  options = options || {};

  var byteString = atob(dataURI.split(',')[1]);
  var mimeString = options.type || 'image/jpeg';
  var quality = options.quality || 1;

  var ab = new ArrayBuffer(byteString.length);
  var ia = new Uint8Array(ab);

  for (var i = 0; i < byteString.length; i++) {
    ia[i] = byteString.charCodeAt(i);
  }

  return new Blob([ab], { type: mimeString, quality: quality });
}

  $(".rotate-button").on("click", function() {
      var index = $(this).attr("id").replace("rotateButton", "");
      if (croppers[index - 1]) {
          croppers[index - 1].rotate(90);
      }
  });

// 添加拖拽支持
$("#drag-container1, #drag-container2, #drag-container3, #drag-container4, #drag-container5, #drag-container6").on("dragover", function(e) {
  e.preventDefault();
  e.stopPropagation();
});

$("#drag-container1, #drag-container2, #drag-container3, #drag-container4, #drag-container5, #drag-container6").on("dragenter", function(e) {
  e.preventDefault();
  e.stopPropagation();
});

$("#drag-container1, #drag-container2, #drag-container3, #drag-container4, #drag-container5, #drag-container6").on("drop", function(e) {
  e.preventDefault();
  e.stopPropagation();
  var index = $(this).attr("id").replace("drag-container", "");
  var fileInput = $("#file" + index)[0];
  
  // 设置文件输入的值，触发change事件
  fileInput.files = e.originalEvent.dataTransfer.files;
  $(fileInput).trigger("change");
});

function showMessage(message, isError = false) {
  const messageBox = $("#messageBox");
  messageBox.text(message);

  if (isError) {
  messageBox.css("background-color", "#f44336"); // 红色背景，表示错误消息
  } else {
  messageBox.css("background-color", "#4CAF50"); // 绿色背景，表示成功消息
  }

  messageBox.show();

  // 自动隐藏消息框
  setTimeout(function() {
  messageBox.hide();
  }, 2000); // 3秒后自动隐藏
}
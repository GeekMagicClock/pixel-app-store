function set_key(){
  var x=getE("api");
  var url = "/set?key="+x.value.replace(/ /g,"");
  send_http(url);
}			
function set_unit(){
  var x=getE("windspeed");
  var y=getE("temp");
  var z=getE("pressure");
  var url = "/set?w_u="+x.value.replace(/ /g,"")+"&t_u="+y.value.replace(/ /g,"")+"&p_u="+z.value.replace(/ /g,"");
  send_http(url);
}			
function set_weather(){
  var x=getE("city1");
  if(x.value == ""){
    alert("city location or city code can't be empty.");
    var ret = getE("ret");
    ret.innerHTML="Please input city location or city code."
  }else{
      url = "/set?cd1="+x.value.replace(/ /g,"")+"&cd2=1000";
      getResponse(url, function(responseText) {
      var err = getE("error");
      if (responseText == "OK") {
        err.style.background="#5ecb4a";
        showMessage("Saved Successfully!", 1000);
      }else{
        //ele.style.visibility = 'visible';
        showMessage("Save faild!",1000);
      } 
    });
  }	
}
function set_weather_itv(){
  var interval = getE('weather_interval').value;
  url = "/set?w_i="+interval.replace(/ /g,"");
  send_http(url);
}


function set_time_brt(){
  var t1=getE("time1");
  var t2=getE("time2");
  var b2=getE("time_brt");
  var enable=getE("time_brt_en");
  if(enable.checked == true){
    en = 1;
  }else{
    en = 0;
  }
  var url = "/set?t1="+t1.value+"&t2="+t2.value+"&b1=50&b2="+b2.value+"&en="+en;
  send_http(url);
}

function setTheme(th){
  var url = "/set?theme="+th;
  send_http(url);
}			

function set_theme_list(){
  var arr = new Array();
  if(getE("th1").checked) arr[0] = 1;else arr[0] = 0;
  if(getE("th2").checked) arr[1] = 1;else arr[1] = 0;
  if(getE("th3").checked) arr[2] = 1;else arr[2] = 0;
  if(getE("th4").checked) arr[3] = 1;else arr[3] = 0;
  if(getE("th5").checked) arr[4] = 1;else arr[4] = 0;
  if(getE("th6").checked) arr[5] = 1;else arr[5] = 0;
  if(getE("th7").checked) arr[6] = 1;else arr[6] = 0;
  if(getE("sw_en").checked) sw = 1;
  else sw = 0;
  if(sw==1){
    var n=0;
    for(i=0;i<9;i++){
      if (arr[i] == 1) n++;
    }
    if(n<2){
      alert("At least please choose two themes!");
      return;
    }
  }
  var x = getE("theme_interval");
  var url = "/set?theme_list="+arr+"&sw_en="+sw+"&theme_interval="+x.value;
  send_http(url);
}
function set_reset(){
  var url = "/set?reset=1";
  send_http(url);
}
function set_reboot(){
  var url = "/set?reboot=1";
  send_http(url);
}
function set_brt(){
  var brt = getE('brt').value;
  var url = "/set?brt="+brt.replace(/ /g,"");
  send_http(url);	
}
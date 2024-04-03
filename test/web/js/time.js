function setDayFormat(ele){
  var url = "/set?day="+ele.value;
  send_http(url);	
}
function set_timecolor(){
  var hc = getE('hc');
  var mc = getE('mc');
  var sc = getE('sc');				
  var url = "/set?hc="+escape(hc.value)+"&mc="+encodeURIComponent(mc.value)+"&sc="+encodeURIComponent(sc.value);
  console.log(url);
  send_http(url);				
}		
function set_hour12(){
  var hour12 = getE('hour12').checked;
  if(hour12 == true) hour=1;
  else hour=0;
  var url = "/set?hour="+hour;
  console.log(url);
  send_http(url);				
}
function set_font(){
  var font = getE('font').value;
  var url = "/set?font="+font;
  console.log(url);
  send_http(url);				
}			
function set_colon(){
  var colon = getE('colon').checked;
  if(colon == true) colon=1;
  else colon=0;
  var url = "/set?colon="+colon;
  console.log(url);
  send_http(url);				
}
function set_ntp(){
  var ntp = getE('ntp').value;
  var url = "/set?ntp="+ntp.replace(/ /g,"");
  console.log(url);
  send_http(url);				
}
function set_interval(){
  var interval = getE('time_interval').value;
  var url = "/set?time_interval="+interval.replace(/ /g,"");
  console.log(url);
  send_http(url);				
}
function set_dst(){				
  var dst = getE('dst').checked;
  if(dst == true) dst=1;
  else dst=0;
  var url = "/set?dst="+dst;
  console.log(url);
  send_http(url);				
}
function set_daytimer(){
  var yr = getE('yr').value.replace(/ /g,"");
  var mth = getE('mth').value.replace(/ /g,"");
  var day = getE('day').value.replace(/ /g,"");
  if(!Number.isInteger(parseInt(yr)) || !Number.isInteger(parseInt(mth)) || !Number.isInteger(parseInt(day)) || parseInt(day)>31 || parseInt(mth) >12 )
  { alert("Please check the date is valid！");return;}
  
  var url = "/set?yr="+yr+"&mth="+mth+"&day="+day;
  console.log(url);
  send_http(url);				
}
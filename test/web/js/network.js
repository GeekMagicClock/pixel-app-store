function scan() {
    var table = document.getElementsByTagName('table')[0];
    var networkInfo = getE('networksFound');
    var scanInfo = getE('scanInfo');
    scanInfo.style.visibility = 'visible';
    getResponse("wifi.json", function(responseText) {
        scanInfo.style.visibility = 'hidden';
        var res = JSON.parse(responseText);
        networkInfo.innerHTML = res.aps.length;
        var tr = '';
        if (res.aps.length > 0) {
            tr += '<tr><th>Channel</th><th>WiFi</th><th>Safe</th><th>Signal</th><th>SELECT</th></tr>';
        }	
        
        for (var i = 0; i < res.aps.length; i++) {
          if (res.aps[i].se == 1) tr += '<tr class="selected">';
          else tr += '<tr>';
          tr += '<td>' + res.aps[i].c + '</td>';
          tr += '<td>' + escapeHTML(res.aps[i].ss) + '</td>';
          if(res.aps[i].e){
            tr += '<td>' + "&#128274;" + '</td>';
          }else{
            tr += '<td>' + " " + '</td>';
          }
          var _width = res.aps[i].r + 130;
          var _color;
          if(res.aps[i].r < -80) _color = "meter_red";
          else if(res.aps[i].r < -60) _color = "meter_orange";
          else _color = "meter_green";
          tr += '<td><div class="meter_background"> <div class="meter_forground '+_color+'" style="width: '+_width+'%;"><div class="meter_value">' + res.aps[i].r + '</div></div> </div></td>';
          tr += '<td><button class="select" onclick="var ssid= \''+escapeHTML(res.aps[i].ss)+'\';select(ssid);">SELECT</button></td>';
          tr += '</tr>';
        }
        table.innerHTML = tr;
  });
}
function set_delay(){
    var delay = getE('delay').value;
    var url = "/set?delay="+delay.replace(/ /g,"");
    send_http(url);				
}
function set_wifi(data){
    var ssid;
    var pwd;
    if(data == "scan"){
        ssid = getE('scan_ssid').value;
        pwd = getE('scan_pwd').value;
    }else{
        ssid = getE('ssid').value;
        pwd = getE('pwd').value;
    }
    var url = "/wifisave?s="+encodeURIComponent(ssid)+"&p="+encodeURIComponent(pwd);
    send_http(url);				
}
function select(ssid) {
    scan_ssid.value = ''+(ssid)+'';
    scan_pwd.value = '';
}
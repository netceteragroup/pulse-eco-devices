const char STATUS_page[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head><title>pulse.eco device</title>
<style>
body {font-family: 'Verdana'; padding: 2em;}
div, p {font-size: 2em;}
p {font-weight: bold;}
h1 {font-size: 3em; text-align: center;}
p, h1 {color: rgb(33, 173, 208);}
input[type='submit'] {font-size: 1.5em;width: 100%;}
.line {
 background-color: rgb(33, 173, 208);
 line-height: 1;
 height: 7px;
 width: 10%;
 margin-top:-0.5em;
 margin-bottom:0.5em;
}
</style></head>
<body>
<h1>pulse.eco Wi-Fi v2</h1>
<h1>DEVICE STATUS</h1>
<p>LAST MEASUREMENT</p>
<div class='line'></div>
<div>PM10: <a id="pm10"></a></div>
<div>PM2.5: <a id="pm25"></a></div>
<div>Temperature: <a id="temperature"></a></div>
<div>Humidity: <a id="humidity"></a></div>
<div>Pressure: <a id="pressure"></a></div>
<div>Noise: <a id="noise"></a></div>
<p>CONNECTION</p>
<div class='line'></div>
<div>Wi-Fi network: <a id="network"></div>
<div>Data packets sent since last reboot: <a id="packets"></div>
<p>PERIPHERALS</p>
<div class='line'></div>
<div>BME sensor: <a id="bme"></div>
<div>SDS011 sensor: <a id="sds"></div>
<div>Noise sensor: <a id="snoise"></div>
<p>REBOOT</p>
<div class='line'></div>
<form method='post' action='/reboot'><div><input type='submit' value='Reboot device' /></div></form>
<p>HARDWARE RESET</p>
<div class='line'></div>
<div><a href="/reset">Hardware reset</a></div>
<script>
 fetch('/values', {method: 'GET'}).then((res) => res.text()).then((res) => {
  res.split(';').forEach((pair) => {
   var ar = pair.split('=');
   document.getElementById(ar[0]).textContent=ar[1];
  });
 });
</script>
</body>
</html>
)=====";
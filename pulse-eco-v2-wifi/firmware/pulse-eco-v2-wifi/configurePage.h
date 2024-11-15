const char CONFIGURE_page[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
<title>pulse.eco device config</title>
<style>
 body {font-family: 'Verdana'; padding: 2em;}
 input[type='text'] { font-size: 2em; width: 100%;}
 input[type='radio'] { display: none;}
 input[type='radio'] {
  height: 2.5em; width: 2.5em; display: inline-block;cursor: pointer;
  vertical-align: middle; background: #FFF; border: 1px solid #d2d2d2; border-radius: 100%;}
 input[type='radio'] { border-color: #c2c2c2;}
 input[type='radio']:checked { background:gray;}
 div, p {font-size: 2em;}
 p {font-weight: bold;}
 input[type='submit'], button {font-size: 2em; width: 100%;}
 .line {
  background-color: rgb(33, 173, 208);
  line-height: 1;
  height: 7px;
  width: 10%;
  margin-top:-0.5em;
  margin-bottom:0.5em;
 }
 h1 {font-size: 3em;}
 h2 {font-size: 2em;}
 p, h1, h2 {color: rgb(33, 173, 208);}
</style>
<script>
 var checkInterval = 30000;
 var checkTimeout = 10000;
 
 function bset(key, val) {document.getElementById("submit").setAttribute(key, val);}
 
 document.addEventListener('submit', (e) => {
  const form = e.target;
   fetch(form.action, {
    method: form.method,
    body: new FormData(form),
  });
  e.preventDefault();
  bset('disabled', 'disabled');
  bset('value', 'Verifying, please wait...');
  setTimeout(checkConnectivity, checkInterval);       
 });

 var connectivityCheck = 2;
 function checkConnectivity() {
  if (!connectivityCheck) {
   bset('value', 'Verification failed!');
   return;
  }
  connectivityCheck--;

  const xhr = new XMLHttpRequest();
  xhr.open('GET', 'http://pulse-eco.local/check', true);
  xhr.timeout = checkTimeout;
  xhr.onload = () => {
   bset('value', 'Connected! Redirecting...');
   document.location = "http://pulse-eco.local/";
  };
  xhr.ontimeout = (e) => {
   setTimeout(checkConnectivity, checkInterval);
  };
  xhr.send(null); 
 }
</script>
</head>
<body>
 <h1 style='text-align: center;'>pulse.eco Wi-Fi v2</h1>
 <h1 style='text-align: center;'>DEVICE CONFIG</h1>
 <br/>
 <form method='post' action='/post'>
  <p>PULSE.ECO CREDENTIALS</p>
  <div class='line'></div>
  <div>Device key:</div>
  <div><input type='text' name='deviceId' required="required" /><br/><br/></div>
  <p>HOME WI-FI INFO</p>
  <div class='line'></div>
  <div>Wi-Fi Network Name (SSID):</div>
  <div><input type='text' name='ssid' required="required" /><br/><br/></div>
  <div>Password:</div>
  <div><input type='text' name='password' required="required" /><br/><br/></div>
  <div><input id='submit' type='submit' value='Save & Connect' /></div>
 </form>
</body>
</html>
)=====";

const char REBOOT_page[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
<title>pulse.eco device reboot</title>
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
 function redirectBack() {
  document.location = "http://pulse-eco.local/";
 }
 setTimeout(redirectBack, 30000);
</script>
</head>
<body>
 <h1 style='text-align: center;'>pulse.eco Wi-Fi v2</h1>
 <h1 style='text-align: center;'>DEVICE REBOOT</h1>
 <br/>
 <p>INFO</p>
  <div class='line'></div>
  <div style="text-align: center;">The device is rebooting. Redirecting back in half a minute. </div>
</body>
</html>
)=====";
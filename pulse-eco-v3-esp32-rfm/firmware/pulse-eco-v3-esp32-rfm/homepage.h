const char homepage[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
    <title>Choose how you will use this pulse eco device</title>
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

        input[type='submit'],
        button,
        a.button {
            font-size: 2em;
            width: 100%;
            margin-bottom: 1em;
            text-align: center;
            text-decoration: none;
            display: inline-block;
            padding: 0.5em;
            border: 1px solid #d2d2d2;
            border-radius: 5px;
            background-color: #FFF;
            color: rgb(33, 173, 208);
        }

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
</head>
<body>
    <h1 style='text-align: center;'>pulse.eco unified TTGO v3</h1>
    <h1 style='text-align: center;'>DEVICE CONFIG</h1>

    <h1>Choose how you will use this pulse eco device</h1>
    
    <div class="line"></div>
    
    <a type="button" href="/lorawan" class="button">LoraWAN</a>
    <a type="button" href="/wifi" class="button">WiFi</a>
</body>
</html>
)=====";

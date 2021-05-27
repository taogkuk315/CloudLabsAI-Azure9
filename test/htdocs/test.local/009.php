<!DOCTYPE html>
<html>
<head>
<meta charset="ISO-8859-1">
<title>HTML/2.0 Test File: 009</title>
</head>
<body>
<?php
set_time_limit(0);
	echo "<h1>HTML/2.0 Test File: 009</h1>";
	echo "This page shows a timestamp every 1 second from the server.<br>";
	echo "--------------------------------------------------------------------------------<br>";
	flush();
	ob_flush();
	try{
		while (TRUE){
			echo date('m-d-Y H:i:s')."<br>";
			sleep ( 1 );
			flush();
			ob_flush();
		}
	} catch(Exception $e){
		// Nothing to do
	}
?>
</body>
</html>

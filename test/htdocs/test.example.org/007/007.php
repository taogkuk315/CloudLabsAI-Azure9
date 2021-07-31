<!DOCTYPE html>
<html>
<head>
	<meta charset="ISO-8859-1">
	<title>HTML/2.0 Test File: 007 (received data)</title>
</head>
<body>
	<h1>HTML/2.0 Test File: 007</h1>
	<h2>Data processed:</h2>
	<p>
		<?php
			function getReceivedVar($label) {
				if (isset($_POST[$label])) {
					echo $_POST[$label];
				} else {
					echo "Not received";
				}
			}
			echo "<ul>";
			echo "<li>HTML form page name: ";
			echo getReceivedVar('pageName') . "<br>";
			echo "<li>User name: ";
			echo getReceivedVar('pName') . "<br>";
			echo "<li>User age: ";
			echo getReceivedVar('pAge') . "<br>";
			echo "<li>User gender: ";
			echo getReceivedVar('pGender');
			echo "</ul>";
			echo "<h2>POST data output:<br></h2>";
			echo var_dump($_POST);
		?>
	</p>
</body>
</html>



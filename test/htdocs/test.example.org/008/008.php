<!DOCTYPE html>
<html>
<head>
	<meta charset="ISO-8859-1">
	<title>HTML/2.0 Test File: 008 (received file)</title>
</head>
<body>
	<h1>HTML/2.0 Test File: 008</h1>
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
		if(is_uploaded_file($_FILES['imageFile']['tmp_name'])){
			echo "<li>File uploaded: ".$_FILES['imageFile']['name'];
			echo "</ul>";
			move_uploaded_file($_FILES['imageFile']['tmp_name'], "008_img");
			echo "<img src=008_img><br>";
		} else {
			echo "<li>File NOT uploaded";
			echo "</ul>";
		}
		echo "<h2>FILE data output:<br></h2>";
		echo var_dump($_FILES);
		echo "<h2>POST data output:<br></h2>";
		echo var_dump($_POST);
		?>
	</p>
</body>
</html>


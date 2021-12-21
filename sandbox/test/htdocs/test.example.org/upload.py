#!/usr/bin/env python
import cgi, os
import cgitb; cgitb.enable()

status = '200 Ok'

try: # Windows needs stdio set for binary mode.
    import msvcrt
    msvcrt.setmode (0, os.O_BINARY) # stdin  = 0
    msvcrt.setmode (1, os.O_BINARY) # stdout = 1
except ImportError:
    pass

try:
    form = cgi.FieldStorage()

    # A nested FieldStorage instance holds the file
    fileitem = form['file']

    # Test if the file was uploaded
    if fileitem.filename:
        
        # strip leading path from file name to avoid directory traversal attacks
        fn = os.path.basename(fileitem.filename)
        open('./files/' + fn, 'wb').write(fileitem.file.read())
        message = 'The file "' + fn + '" was uploaded successfully'

    else:
        status = '400 Parameter Missing'
        message = 'No file was uploaded'

except KeyError:
    message = '''\
        Upload File<form method="POST" enctype="multipart/form-data">
        <input type="file" name="file">
        <button type="submit">Upload</button></form>
        '''
    pass

print "Status: %s" % (status,)
print """\
    Content-Type: text/html\n
    <html><body>
    <p>%s</p>
    </body></html>""" % (message,)

Option Explicit
Dim shell, files, root, node, code
Set shell = CreateObject("WScript.Shell")
Set files = CreateObject("Scripting.FileSystemObject")
root = files.GetParentFolderName(WScript.ScriptFullName)
node = files.BuildPath(root, "data\runtime\node\node.exe")
If Not files.FileExists(node) Or Not files.FileExists(files.BuildPath(root, "data\state\installation.json")) Then
  shell.CurrentDirectory = root
  code = shell.Run("""" & files.BuildPath(root, "Start.cmd") & """", 1, True)
  WScript.Quit code
End If
shell.CurrentDirectory = root
code = shell.Run("""" & node & """ """ & files.BuildPath(root, "distribution\start-windows.cjs") & """", 0, True)
If code <> 0 Then
  MsgBox "IZ2S could not open. See data\state\startup.log in:" & vbCrLf & root, 48, "IZ2S Platform"
End If
WScript.Quit code

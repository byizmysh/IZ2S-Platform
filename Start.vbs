Option Explicit
Dim shell, files, root, node, code
Set shell = CreateObject("WScript.Shell")
Set files = CreateObject("Scripting.FileSystemObject")
root = files.GetParentFolderName(WScript.ScriptFullName)
node = files.BuildPath(root, "data\runtime\node\node.exe")
If Not files.FileExists(node) Then
  MsgBox "Open Install.cmd to install IZ2S Platform first.", 48, "IZ2S Platform"
  WScript.Quit 1
End If
shell.CurrentDirectory = root
code = shell.Run("""" & node & """ """ & files.BuildPath(root, "distribution\start-local.cjs") & """", 0, True)
If code <> 0 Then
  MsgBox "IZ2S could not open. See data\state\startup.log in:" & vbCrLf & root, 48, "IZ2S Platform"
End If
WScript.Quit code

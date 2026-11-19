"""Build a per-user WiX 3 MSI from a verified review directory on Windows."""
import argparse
import hashlib
import subprocess
import tempfile
import uuid
import xml.etree.ElementTree as ET
from pathlib import Path

NS = "http://schemas.microsoft.com/wix/2006/wi"
UPGRADE = "E1685777-A78D-46B3-BB8A-34AD25EE46D9"
ET.register_namespace("",NS)
def node(parent,tag,**attrs):
    return ET.SubElement(parent,f"{{{NS}}}{tag}",attrs)
def key(path):
    return hashlib.sha256(path.encode("utf-8")).hexdigest()[:26]
def guid(path):
    return str(uuid.uuid5(uuid.UUID(UPGRADE),path)).upper()

def build(kit,wix,out,version):
    listed=[]
    for line in (kit/"SHA256SUMS").read_text(encoding="utf-8").splitlines():
        expected,name=line.split("  ",1)
        path=(kit/name).resolve()
        if not path.is_relative_to(kit):raise ValueError("Invalid package path")
        with path.open("rb") as f:
            if hashlib.file_digest(f,"sha256").hexdigest()!=expected:raise ValueError(name)
        listed.append(name)
    listed.append("SHA256SUMS")
    root=ET.Element(f"{{{NS}}}Wix")
    product=node(root,"Product",Id=guid("product-"+version),Name="SqlScan",Language="1049",
                 Codepage="1251",Version=version,Manufacturer="SqlScan",UpgradeCode=UPGRADE)
    node(product,"Package",InstallerVersion="500",Compressed="yes",InstallScope="perUser",
         InstallPrivileges="limited",Platform="x64",SummaryCodepage="1251",Description="SqlScan — проверка журналов")
    node(product,"MajorUpgrade",Schedule="afterInstallInitialize",DowngradeErrorMessage="Установлена более новая версия SqlScan.")
    node(product,"MediaTemplate",EmbedCab="yes",CompressionLevel="high")
    target=node(product,"Directory",Id="TARGETDIR",Name="SourceDir")
    appdata=node(target,"Directory",Id="LocalAppDataFolder")
    app=node(appdata,"Directory",Id="INSTALLFOLDER",Name="SqlScan2App")
    menu=node(node(target,"Directory",Id="ProgramMenuFolder"),"Directory",Id="SqlScanMenu",Name="SqlScan")
    node(target,"Directory",Id="System64Folder")
    feature=node(product,"Feature",Id="Complete",Title="SqlScan",Level="1")
    dirs={".":app}
    def directory(rel):
        name=rel.as_posix()
        if name not in dirs:
            dirs[name]=node(directory(rel.parent),"Directory",Id="d"+key(name),Name=rel.name)
        return dirs[name]
    groups={}
    for name in listed:
        rel=Path(name);parent=rel.parent.as_posix()
        if parent not in groups:
            component=node(directory(rel.parent),"Component",Id="c"+key(parent),Guid=guid(parent),Win64="yes")
            node(component,"RegistryValue",Root="HKCU",Key="Software\\SqlScanCoursework\\Installer",
                 Name="component_"+key(parent),Type="integer",Value="1",KeyPath="yes")
            node(component,"RemoveFolder",Id="r"+key(parent),On="uninstall")
            node(feature,"ComponentRef",Id="c"+key(parent))
            groups[parent]=component
        node(groups[parent],"File",Id="f"+key(name),Source=str(kit/rel),Name=rel.name)
    for name, element in dirs.items():
        if name not in groups:
            node(groups["."],"RemoveFolder",Id="r"+key(name),Directory=element.get("Id"),On="uninstall")
    shortcuts=node(menu,"Component",Id="Shortcuts",Guid=guid("shortcuts"),Win64="yes")
    node(feature,"ComponentRef",Id="Shortcuts")
    for i,(name,file) in enumerate([("SqlScan","SqlScan-Local.cmd"),("Проверка баллов SqlScan","Check-Scores.cmd"),
                                    ("Полная проверка SqlScan","Recheck.cmd")]):
        node(shortcuts,"Shortcut",Id="shortcut"+str(i),Name=name,Target="[INSTALLFOLDER]"+file,WorkingDirectory="INSTALLFOLDER")
    node(shortcuts,"RegistryValue",Root="HKCU",Key="Software\\SqlScanCoursework\\Installer",Name="shortcuts",Type="integer",Value="1",KeyPath="yes")
    node(shortcuts,"RemoveFolder",Id="RemoveMenu",On="uninstall")
    node(product,"CustomAction",Id="StopLocal",Directory="System64Folder",Execute="deferred",
         Impersonate="yes",Return="check",ExeCommand='"[System64Folder]WindowsPowerShell\\v1.0\\powershell.exe" -NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy RemoteSigned -File "[INSTALLFOLDER]Stop-InstalledServer.ps1"')
    sequence=node(product,"InstallExecuteSequence")
    node(sequence,"Custom",Action="StopLocal",Before="RemoveFiles").text='Installed AND REMOVE="ALL"'
    # Use the native installation wizard without adding a fictitious license acceptance.
    node(product,"Property",Id="WIXUI_INSTALLDIR",Value="INSTALLFOLDER")
    node(product,"UIRef",Id="WixUI_InstallDir")
    ui=node(product,"UI")
    node(ui,"Publish",Dialog="WelcomeDlg",Control="Next",Event="NewDialog",Value="InstallDirDlg",Order="9").text="NOT Installed"
    node(ui,"Publish",Dialog="InstallDirDlg",Control="Back",Event="NewDialog",Value="WelcomeDlg",Order="9").text="1"
    with tempfile.TemporaryDirectory(prefix="sqlscan-wix-") as folder:
        folder=Path(folder);src=folder/"product.wxs";obj=folder/"product.wixobj"
        ET.ElementTree(root).write(src,encoding="utf-8",xml_declaration=True)
        subprocess.run([wix/"candle.exe","-nologo","-arch","x64","-out",obj,src],check=True)
        subprocess.run([wix/"light.exe","-nologo","-ext","WixUIExtension","-cultures:ru-ru",
                        "-spdb","-out",out,obj],check=True)
if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    for n in ("kit","wix","out"):parser.add_argument(n,type=Path)
    parser.add_argument("--version",default="2.0.2")
    a=parser.parse_args();build(a.kit.resolve(),a.wix.resolve(),a.out.resolve(),a.version)

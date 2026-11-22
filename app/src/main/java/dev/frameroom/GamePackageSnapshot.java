package dev.frameroom;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/** Copies APK archives only. Never executes a cloned app or copies private user data. */
final class GamePackageSnapshot {
    private GamePackageSnapshot() {}

    static final class Snapshot {
        final String packageName, id, engine, version;
        final long bytes;
        final int files;
        final boolean arm64;
        Snapshot(String pkg, String id, String engine, String version, long bytes, int files, boolean arm64) {
            this.packageName=pkg;this.id=id;this.engine=engine;this.version=version;
            this.bytes=bytes;this.files=files;this.arm64=arm64;
        }
        String report() {
            return files+" arquivos APK, "+String.format(Locale.ROOT,"%.1f MB",bytes/1048576.0)
                +"\nVersão "+version+" • Motor provável: "+engine
                +"\nARM64 dentro do APK: "+(arm64?"sim":"não identificado")
                +"\nNÃO é um clone executável: falta o runtime virtualizado e a ligação Vulkan.";
        }
    }

    static Snapshot prepare(Context context, String packageName) throws Exception {
        if (!packageName.matches("[a-zA-Z0-9_]+(\\.[a-zA-Z0-9_]+)+")) {
            throw new IllegalArgumentException("Nome de pacote inválido.");
        }
        PackageManager pm=context.getPackageManager();
        ApplicationInfo app=pm.getApplicationInfo(packageName,0);
        PackageInfo packageInfo=pm.getPackageInfo(packageName,0);
        if(app.sourceDir==null)throw new IOException("APK principal não disponível.");
        List<File> sources=new ArrayList<>();
        sources.add(new File(app.sourceDir));
        if(app.splitSourceDirs!=null)for(String path:app.splitSourceDirs)
            if(path!=null)sources.add(new File(path));
        long totalSize=0;
        for(File source:sources){
            if(!source.isFile()||!source.canRead())
                throw new IOException("O Android não permite ler "+source.getName());
            totalSize=Math.addExact(totalSize,source.length());
        }
        File root=new File(context.getFilesDir(),"apk_snapshots");
        if(!root.isDirectory()&&!root.mkdirs())throw new IOException("Falha ao criar pasta privada.");
        if(root.getUsableSpace()<totalSize+(128L<<20))
            throw new IOException("Espaço insuficiente: são necessários os APKs mais 128 MB livres.");
        String id=packageName+"__"+System.currentTimeMillis();
        File target=new File(root,id);
        if(!target.mkdir())throw new IOException("Falha ao criar pasta do snapshot.");
        long bytes=0;
        boolean unity=false,unreal=false,arm64=false;
        JSONArray all=new JSONArray();
        try{
            for(int i=0;i<sources.size();i++){
                File source=sources.get(i);
                String name=i==0?"base.apk":String.format(Locale.ROOT,"split-%03d.apk",i);
                File output=new File(target,name);
                MessageDigest sha=MessageDigest.getInstance("SHA-256");
                long copied=0;
                try(InputStream input=new BufferedInputStream(new FileInputStream(source));
                    FileOutputStream raw=new FileOutputStream(output)){
                    // Android 14+: DEX/APK code must be read-only before content is written.
                    if (!output.setReadOnly()) throw new IOException("Não foi possível tornar o APK somente leitura.");
                    OutputStream out=new BufferedOutputStream(raw);
                    byte[] buffer=new byte[131072];
                    int read;
                    while((read=input.read(buffer))!=-1){
                        if(read==0)continue;
                        out.write(buffer,0,read);sha.update(buffer,0,read);copied+=read;
                    }
                    out.flush();
                }
                if(copied!=source.length())throw new IOException("Cópia incompleta: "+name);
                bytes+=copied;
                JSONObject record=new JSONObject();
                record.put("name",name);record.put("bytes",copied);record.put("sha256",hex(sha.digest()));
                all.put(record);
                try(ZipFile zip=new ZipFile(output)){
                    Enumeration<? extends ZipEntry> entries=zip.entries();
                    while(entries.hasMoreElements()){
                        String entry=entries.nextElement().getName();
                        if(entry.startsWith("lib/arm64-v8a/")&&entry.endsWith(".so"))arm64=true;
                        if(entry.endsWith("/libunity.so")||entry.endsWith("/libil2cpp.so"))unity=true;
                        if(entry.endsWith("/libUE4.so")||entry.endsWith("/libUnreal.so"))unreal=true;
                    }
                }
            }
            String engine=unity?"Unity":unreal?"Unreal":"desconhecido";
            String version=packageInfo.versionName==null?"desconhecida":packageInfo.versionName;
            JSONObject metadata=new JSONObject();
            metadata.put("status","APK_COPIED_RUNTIME_NOT_AVAILABLE");
            metadata.put("package",packageName);metadata.put("version",version);
            metadata.put("engine",engine);metadata.put("arm64",arm64);
            metadata.put("bytes",bytes);metadata.put("files",all);
            metadata.put("warning","Não inclui dados privados, OBB ou Play Asset Delivery. Não é executável.");
            try(OutputStreamWriter writer=new OutputStreamWriter(
                new FileOutputStream(new File(target,"snapshot.json")),StandardCharsets.UTF_8)){
                writer.write(metadata.toString(2));
            }
            return new Snapshot(packageName,id,engine,version,bytes,sources.size(),arm64);
        }catch(Exception error){deleteTree(target);throw error;}
    }

    static Snapshot latest(Context context,String packageName){
        File root=new File(context.getFilesDir(),"apk_snapshots");
        File[] dirs=root.listFiles(f->f.isDirectory()&&f.getName().startsWith(packageName+"__"));
        if(dirs==null||dirs.length==0)return null;
        Arrays.sort(dirs,(a,b)->Long.compare(b.lastModified(),a.lastModified()));
        for(File dir:dirs)try{
            JSONObject data=new JSONObject(new String(
                java.nio.file.Files.readAllBytes(new File(dir,"snapshot.json").toPath()),
                StandardCharsets.UTF_8));
            if(!packageName.equals(data.optString("package")))continue;
            return new Snapshot(packageName,dir.getName(),data.optString("engine","desconhecido"),
                data.optString("version","desconhecida"),data.getLong("bytes"),
                data.getJSONArray("files").length(),data.optBoolean("arm64",false));
        }catch(Exception ignored){}
        return null;
    }

    static int remove(Context context,String packageName){
        File root=new File(context.getFilesDir(),"apk_snapshots");
        File[] dirs=root.listFiles(f->f.isDirectory()&&f.getName().startsWith(packageName+"__"));
        if(dirs==null)return 0;
        int removed=0;
        for(File dir:dirs)if(deleteTree(dir))removed++;
        return removed;
    }


    /**
     * Read-only DEX path for a cooperative APK with EXACT package identity and
     * matching signatures. We also verify that the staged APKs still match
     * both the snapshot manifest and the currently installed signed APK.
     */
    static String verifiedDexPath(Context context,String packageName) throws Exception {
        if(!"dev.frameroom.samplegame".equals(packageName))
            throw new SecurityException("APK externo não compatível com runtime cooperativo.");
        PackageManager pm=context.getPackageManager();
        if(pm.checkSignatures(context.getPackageName(),packageName)!=PackageManager.SIGNATURE_MATCH)
            throw new SecurityException("APK de teste com assinatura diferente do FrameRoom.");
        Snapshot selected=latest(context,packageName);
        if(selected==null)throw new IOException("Primeiro prepare uma cópia privada dos APKs.");
        File dir=new File(new File(context.getFilesDir(),"apk_snapshots"),selected.id);
        if(!dir.isDirectory())throw new IOException("Snapshot não encontrado.");
        JSONObject manifest=new JSONObject(new String(
            java.nio.file.Files.readAllBytes(new File(dir,"snapshot.json").toPath()),StandardCharsets.UTF_8));
        if(!packageName.equals(manifest.getString("package")))
            throw new SecurityException("Nome de pacote do snapshot incompatível.");
        JSONArray files=manifest.getJSONArray("files");
        ApplicationInfo info=pm.getApplicationInfo(packageName,0);
        ArrayList<File> installed=new ArrayList<>();
        installed.add(new File(info.sourceDir));
        if(info.splitSourceDirs!=null)for(String path:info.splitSourceDirs)if(path!=null)
            installed.add(new File(path));
        if(files.length()!=installed.size())throw new SecurityException("A divisão de APKs foi alterada.");
        StringJoiner paths=new StringJoiner(File.pathSeparator);
        for(int i=0;i<installed.size();i++){
            JSONObject entry=files.getJSONObject(i);
            String expectedName=i==0?"base.apk":String.format(Locale.ROOT,"split-%03d.apk",i);
            if(!expectedName.equals(entry.getString("name")))
                throw new SecurityException("Arquivo split inesperado.");
            File staged=new File(dir,expectedName);
            if(!staged.isFile())throw new IOException("APK copiado não foi encontrado.");
            if(!staged.setReadOnly())throw new IOException("APK não pôde ser bloqueado para escrita.");
            if(staged.canWrite())throw new SecurityException("APK carregável ainda pode ser modificado.");
            String recorded=entry.getString("sha256");
            if(!recorded.equalsIgnoreCase(digest(staged)) ||
               !recorded.equalsIgnoreCase(digest(installed.get(i))))
                throw new SecurityException("APK copiado não corresponde ao jogo instalado.");
            paths.add(staged.getAbsolutePath());
        }
        return paths.toString();
    }

    private static String digest(File file) throws Exception {
        MessageDigest sha=MessageDigest.getInstance("SHA-256");
        try(InputStream input=new BufferedInputStream(new FileInputStream(file))){
            byte[] buffer=new byte[131072];
            int n;
            while((n=input.read(buffer))!=-1)if(n>0)sha.update(buffer,0,n);
        }
        return hex(sha.digest());
    }

    private static boolean deleteTree(File root){
        File[] children=root.listFiles();
        if(children!=null)for(File child:children)deleteTree(child);
        return root.delete();
    }
    private static String hex(byte[] bytes){
        final char[] c="0123456789abcdef".toCharArray();
        char[] result=new char[bytes.length*2];
        for(int i=0;i<bytes.length;i++){result[i*2]=c[(bytes[i]&255)>>>4];result[i*2+1]=c[bytes[i]&15];}
        return new String(result);
    }
}

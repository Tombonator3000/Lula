// Exports static analysis; generated C is an analysis artifact, not recovered source.
// @category Lula
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.scalar.Scalar;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

public class LulaExport extends GhidraScript {
    private String clean(Object v) { return v == null ? "" : v.toString().replace("\t", " ").replace("\r", " ").replace("\n", "\\n"); }
    private PrintWriter writer(Path p) throws Exception { return new PrintWriter(Files.newBufferedWriter(p, StandardCharsets.UTF_8)); }
    private String functionAt(Address a) { Function f=currentProgram.getFunctionManager().getFunctionContaining(a); return f == null ? "" : f.getEntryPoint()+":"+f.getName(); }
    public void run() throws Exception {
        String[] args=getScriptArgs();
        if(args.length != 1) throw new IllegalArgumentException("Usage: LulaExport.java OUTPUT_DIRECTORY");
        Path output=Paths.get(args[0]); Files.createDirectories(output);
        DecompInterface decompiler=new DecompInterface();
        decompiler.openProgram(currentProgram);
        int total=0,success=0,failed=0,external=0;
        try (PrintWriter c=writer(output.resolve("WET.analysis.c"));
             PrintWriter functions=writer(output.resolve("functions.tsv"));
             PrintWriter calls=writer(output.resolve("callgraph.tsv"));
             PrintWriter symbols=writer(output.resolve("symbols.tsv"));
             PrintWriter importRefs=writer(output.resolve("import-xrefs.tsv"));
             PrintWriter strings=writer(output.resolve("defined-strings.tsv"));
             PrintWriter stringRefs=writer(output.resolve("string-xrefs.tsv"));
             PrintWriter constants=writer(output.resolve("resolution-candidates.tsv"))) {
            c.println("/* GHIDRA STATIC ANALYSIS EXPORT. NOT BUILDABLE ORIGINAL SOURCE.\n * Binary: "+currentProgram.getName()+"; SHA-256: "+currentProgram.getExecutableSHA256()+"\n * Language: "+currentProgram.getLanguageID()+"; compiler spec: "+currentProgram.getCompilerSpec().getCompilerSpecID()+"\n * Undefined types, inferred signatures, lost source names and indirect calls require manual reconstruction. */\n");
            functions.println("entry\tname\tbody_bytes\tthunk\texternal\tdecompiled\terror");
            calls.println("from_function\tfrom_instruction\tto_address\tto_function\treference_type");
            symbols.println("address\tname\tqualified_name\ttype\texternal\tsource");
            importRefs.println("symbol\taddress\tfrom\tfrom_function\treference_type");
            strings.println("address\tdata_type\tvalue");
            stringRefs.println("string_address\tvalue\tfrom\tfrom_function\treference_type");
            constants.println("instruction\tfunction\tscalar_decimal\toperand_index\tdisassembly");
            SymbolIterator si=currentProgram.getSymbolTable().getAllSymbols(true);
            while(si.hasNext() && !monitor.isCancelled()) {
                Symbol s=si.next();
                symbols.println(clean(s.getAddress())+"\t"+clean(s.getName())+"\t"+clean(s.getName(true))+"\t"+clean(s.getSymbolType())+"\t"+s.isExternal()+"\t"+clean(s.getSource()));
                if(s.isExternal() || s.getName().matches(".*(?:DirectDraw|DirectSound|Smack|CreateFile|ReadFile|LoadLibrary|GetProcAddress|FindResource|LoadResource).*")) {
                    for(Reference r:s.getReferences()) importRefs.println(clean(s.getName(true))+"\t"+clean(s.getAddress())+"\t"+r.getFromAddress()+"\t"+clean(functionAt(r.getFromAddress()))+"\t"+r.getReferenceType());
                }
            }
            DataIterator di=currentProgram.getListing().getDefinedData(true);
            while(di.hasNext() && !monitor.isCancelled()) {
                Data d=di.next();
                if(!d.hasStringValue()) continue;
                String value=clean(d.getValue());
                strings.println(d.getAddress()+"\t"+clean(d.getDataType().getName())+"\t"+value);
                ReferenceIterator refs=currentProgram.getReferenceManager().getReferencesTo(d.getAddress());
                while(refs.hasNext()) { Reference r=refs.next(); stringRefs.println(d.getAddress()+"\t"+value+"\t"+r.getFromAddress()+"\t"+clean(functionAt(r.getFromAddress()))+"\t"+r.getReferenceType()); }
            }
            Set<Long> sizes=new HashSet<>(Arrays.asList(640L,480L,320L,240L,800L,600L,1024L,768L,307200L,614400L));
            InstructionIterator ii=currentProgram.getListing().getInstructions(true);
            while(ii.hasNext() && !monitor.isCancelled()) {
                Instruction ins=ii.next();
                for(int op=0;op<ins.getNumOperands();op++) for(Object obj:ins.getOpObjects(op)) if(obj instanceof Scalar) {
                    long value=((Scalar)obj).getUnsignedValue();
                    if(sizes.contains(value)) constants.println(ins.getAddress()+"\t"+clean(functionAt(ins.getAddress()))+"\t"+value+"\t"+op+"\t"+clean(ins));
                }
                for(Reference r:ins.getReferencesFrom()) if(r.getReferenceType().isCall()) calls.println(clean(functionAt(ins.getAddress()))+"\t"+ins.getAddress()+"\t"+r.getToAddress()+"\t"+clean(functionAt(r.getToAddress()))+"\t"+r.getReferenceType());
            }
            FunctionIterator fi=currentProgram.getFunctionManager().getFunctions(true);
            while(fi.hasNext() && !monitor.isCancelled()) {
                Function f=fi.next(); total++;
                if(f.isExternal()) {external++; functions.println(f.getEntryPoint()+"\t"+clean(f.getName())+"\t"+f.getBody().getNumAddresses()+"\t"+f.isThunk()+"\ttrue\tfalse\texternal"); continue;}
                DecompileResults r=decompiler.decompileFunction(f,60,monitor);
                boolean ok=r.decompileCompleted() && r.getDecompiledFunction()!=null;
                if(ok) {success++; c.println("/* "+f.getEntryPoint()+" | "+clean(f.getName())+" | body bytes: "+f.getBody().getNumAddresses()+" */"); c.println(r.getDecompiledFunction().getC());}
                else {failed++; c.println("/* DECOMPILATION INCOMPLETE: "+f.getEntryPoint()+" "+clean(f.getName())+"; "+clean(r.getErrorMessage())+" */");}
                functions.println(f.getEntryPoint()+"\t"+clean(f.getName())+"\t"+f.getBody().getNumAddresses()+"\t"+f.isThunk()+"\tfalse\t"+ok+"\t"+clean(r.getErrorMessage()));
                if(total%250==0) println("Lula export: "+total+" functions, "+success+" completed, "+failed+" failed");
            }
        } finally {decompiler.dispose();}
        try(PrintWriter metadata=writer(output.resolve("summary.json"))) {
            metadata.println("{\n  \"program\": \""+currentProgram.getName()+"\",\n  \"sha256\": \""+currentProgram.getExecutableSHA256()+"\",\n  \"compiler_spec\": \""+currentProgram.getCompilerSpec().getCompilerSpecID()+"\",\n  \"language\": \""+currentProgram.getLanguageID()+"\",\n  \"functions\": "+total+",\n  \"decompiled\": "+success+",\n  \"failed\": "+failed+",\n  \"external\": "+external+",\n  \"cancelled\": "+monitor.isCancelled()+",\n  \"buildable_source\": false\n}");
        }
        println("Lula export finished: "+success+"/"+total+" functions decompiled, "+failed+" incomplete.");
    }
}

const enc=new TextEncoder(),xml=v=>String(v).replace(/[\u0000-\u0008\u000b\u000c\u000e-\u001f]/g,'').replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;').replaceAll('"','&quot;');
const crcTable=Array.from({length:256},(_,n)=>{for(let i=0;i<8;i++)n=n&1?0xedb88320^(n>>>1):n>>>1;return n>>>0;});
const crc32=async blob=>{let c=0xffffffff,total=0;const reader=blob.stream().getReader();for(;;){const {value,done}=await reader.read();if(done)break;for(const x of value)c=crcTable[(c^x)&255]^(c>>>8);total+=value.length;if(total>=1048576){total=0;await new Promise(r=>setTimeout(r,0));}}return (c^0xffffffff)>>>0;};
const header=(size,values)=>{const a=new Uint8Array(size),d=new DataView(a.buffer);for(const [at,value,bits]of values)bits===16?d.setUint16(at,value,true):d.setUint32(at,value,true);return a;};
export async function zipFiles(files,type='application/zip'){
 const parts=[],central=[];let offset=0;
 for(const file of files){
  const name=enc.encode(file.name),raw=file.data instanceof Blob?file.data:new Blob([file.data]),crc=await crc32(raw);let bytes=raw,method=0;
  try{if(typeof CompressionStream==='function'){bytes=await new Response(raw.stream().pipeThrough(new CompressionStream('deflate-raw'))).blob();method=8;}}catch{bytes=raw;}
  const local=header(30,[[0,0x04034b50],[4,20,16],[6,0x800,16],[8,method,16],[12,33,16],[14,crc],[18,bytes.size],[22,raw.size],[26,name.length,16]]);
  const cd=header(46,[[0,0x02014b50],[4,20,16],[6,20,16],[8,0x800,16],[10,method,16],[14,33,16],[16,crc],[20,bytes.size],[24,raw.size],[28,name.length,16],[42,offset]]);
  parts.push(local,name,bytes);central.push(cd,name);offset+=local.length+name.length+bytes.size;file.data=null;
  await new Promise(r=>setTimeout(r,0));
 }
 const length=central.reduce((n,x)=>n+x.length,0),end=header(22,[[0,0x06054b50],[8,files.length,16],[10,files.length,16],[12,length],[16,offset]]);
 return new Blob([...parts,...central,end],{type});
}
const cell=v=>{if(v===null||v===undefined)return '';if(typeof v==='number')return Number.isFinite(v)?String(v):'';let s=String(v);if(/^[=+\-@\t\r]/.test(s))s="'"+s;return '"'+s.replaceAll('"','""')+'"';};
const title=s=>s.replace(/^engineering\.accelerationG\.([012])$/,(_,n)=>'Acceleration '+['X','Y','Z'][n]+' (g)').replace(/^engineering\.(rawGyroDps|correctedGyroDps)\.([012])$/,(_,k,n)=>(k==='rawGyroDps'?'Raw':'Corrected')+' gyro '+['X','Y','Z'][n]+' (deg/s)').replace(/([a-z])([A-Z])/g,'$1 $2').replaceAll('.',' · ');
export function csvFile(table){return '\ufeff'+[table.headers.map(title),...table.rows].map(row=>row.map(cell).join(',')).join('\r\n')+'\r\n';}
const column=n=>{let s='';for(n++;n;n=Math.floor((n-1)/26))s=String.fromCharCode(65+(n-1)%26)+s;return s;};
export async function excelFile(tables){
 const ns='http://schemas.openxmlformats.org/spreadsheetml/2006/main',files=[];
 const sheet=async(table,index)=>{
  const widths=table.headers.map(h=>Math.min(46,Math.max(14,title(h).length+2))),rows=[],chunks=[];
  for(const [r,values]of [table.headers.map(title),...table.rows].entries()){
   let cells='';for(let c=0;c<values.length;c++){
    const v=values[c];if(v===null||v===undefined||v==='')continue;const numeric=typeof v==='number'&&Number.isFinite(v),s=String(v);
    widths[c]=Math.min(46,Math.max(widths[c]||14,Math.min(40,s.length)+2));
    cells+='<c r="'+column(c)+(r+1)+'" s="'+(r===0?1:numeric?2:0)+'" t="'+(numeric?'n':'inlineStr')+'">'+(numeric?'<v>'+v+'</v>':'<is><t xml:space="preserve">'+xml(s)+'</t></is>')+'</c>';
   }
   const height=r===0?45:Math.min(120,Math.max(18,...values.map((v,c)=>Math.ceil(String(v??'').length/Math.max(12,widths[c]-2))*15+3)));
   rows.push('<row r="'+(r+1)+'" ht="'+height+'" customHeight="1">'+cells+'</row>');
   if(r%400===0){chunks.push(enc.encode(rows.join('')));rows.length=0;await new Promise(resolve=>setTimeout(resolve,0));}
  }
  const range='A1:'+column(table.headers.length-1)+(table.rows.length+1);
  chunks.push(enc.encode(rows.join('')));
  const prefix='<?xml version="1.0" encoding="UTF-8"?><worksheet xmlns="'+ns+'"><dimension ref="'+range+'"/><sheetViews><sheetView workbookViewId="0"><pane ySplit="1" topLeftCell="A2" state="frozen"/></sheetView></sheetViews><cols>'+widths.map((w,i)=>'<col min="'+(i+1)+'" max="'+(i+1)+'" width="'+w+'" bestFit="1" customWidth="1"/>').join('')+'</cols><sheetData>';
  files.push({name:'xl/worksheets/sheet'+index+'.xml',data:new Blob([prefix,...chunks,'</sheetData><autoFilter ref="'+range+'"/></worksheet>'])});
 };
 for(let i=0;i<tables.length;i++){await sheet(tables[i],i+1);await new Promise(r=>setTimeout(r,0));}
 files.push({name:'[Content_Types].xml',data:'<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>'+tables.map((_,i)=>'<Override PartName="/xl/worksheets/sheet'+(i+1)+'.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>').join('')+'</Types>'});
 files.push({name:'_rels/.rels',data:'<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>'});
 files.push({name:'xl/workbook.xml',data:'<workbook xmlns="'+ns+'" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets>'+tables.map((t,i)=>'<sheet name="'+xml(t.name)+'" sheetId="'+(i+1)+'" r:id="rId'+(i+1)+'"/>').join('')+'</sheets></workbook>'});
 files.push({name:'xl/_rels/workbook.xml.rels',data:'<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'+tables.map((_,i)=>'<Relationship Id="rId'+(i+1)+'" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet'+(i+1)+'.xml"/>').join('')+'<Relationship Id="styles" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/></Relationships>'});
 files.push({name:'xl/styles.xml',data:'<styleSheet xmlns="'+ns+'"><fonts count="2"><font><sz val="11"/><name val="Calibri"/></font><font><b/><color rgb="FFFFFFFF"/><sz val="11"/><name val="Calibri"/></font></fonts><fills count="3"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill><fill><patternFill patternType="solid"><fgColor rgb="FF17263A"/></patternFill></fill></fills><borders count="1"><border/></borders><cellStyleXfs count="1"><xf/></cellStyleXfs><cellXfs count="3"><xf xfId="0" fontId="0" fillId="0" borderId="0" applyAlignment="1"><alignment vertical="top" wrapText="1"/></xf><xf xfId="0" fontId="1" fillId="2" borderId="0" applyAlignment="1"><alignment vertical="center" wrapText="1"/></xf><xf xfId="0" numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellXfs><cellStyles count="1"><cellStyle name="Normal" xfId="0" builtinId="0"/></cellStyles></styleSheet>'});
 return zipFiles(files,'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet');
}
export function download(blob,name){const url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download=name;document.body.append(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),30000);}

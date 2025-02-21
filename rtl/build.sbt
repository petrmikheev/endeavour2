ThisBuild / scalaVersion := "2.12.18"
ThisBuild / libraryDependencySchemes += "org.scala-lang.modules" %% "scala-xml" % VersionScheme.Always

val spinalVersion = "dev"
val spinalHdlPath = new File("VexiiRiscv/ext/SpinalHDL").getAbsolutePath

lazy val endeavour2 = project
  .in(file("."))
  .settings(
    scalacOptions += s"-Xplugin:${new File(spinalHdlPath + s"/idslplugin/target/scala-2.12/spinalhdl-idsl-plugin_2.12-$spinalVersion.jar")}",
    scalacOptions += s"-Xplugin-require:idsl-plugin",
    scalacOptions += "-language:reflectiveCalls",
    name := "Endeavour2"
  )
  .dependsOn(VexiiRiscv)

lazy val VexiiRiscv = ProjectRef(new File("VexiiRiscv"), "ret")

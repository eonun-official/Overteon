#!/usr/bin/env ruby
require 'open3'

abort 'Usage: ruby Tools/units/check-shaders.rb QSB SHADER_DIRECTORY' unless ARGV.size == 2
qsb, directory = ARGV

%w[blur glass].product(%w[vert frag]).each do |name, extension|
  filename = File.join(directory, "#{name}.#{extension}.qsb")
  dump, status = Open3.capture2e(qsb, '--dump', filename)
  abort "#{filename}: #{dump}" unless status.success?

  stage = extension == 'vert' ? 'Vertex' : 'Fragment'
  abort "#{filename}: expected #{stage} stage" unless dump.lines.first&.strip == "Stage: #{stage}"

  variants = dump.lines.grep(/^\s+Shader \d+:/)
  %w[SPIR-V HLSL MSL].each do |target|
    pattern = /:\s+#{Regexp.escape(target)}\s+.*\[Standard\]/
    abort "#{filename}: missing #{target} variant" unless variants.any? { |line| line.match?(pattern) }
  end
  abort "#{filename}: unexpected GLSL variant" if variants.any? { |line| line.include?("GLSL") }
  puts "#{filename}: #{stage}, SPIR-V/HLSL/MSL; no GLSL"
end
